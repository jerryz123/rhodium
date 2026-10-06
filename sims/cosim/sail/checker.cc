// Checks scalar/vector register and memory effects against Sail without reference repair.
// SPDX-License-Identifier: Apache-2.0
#include "checker.h"
#include "memory.h"
#include <stdexcept>
#include <utility>

namespace rhodium::cosim {
namespace {
using namespace observation;
std::uint64_t word(std::span<const std::uint8_t> bytes) {
  if (bytes.size() > 8) throw std::runtime_error("scalar effect exceeds XLEN");
  std::uint64_t result = 0;
  for (std::size_t i = 0; i != bytes.size(); ++i) result |= std::uint64_t(bytes[i]) << (8 * i);
  return result;
}
}
SailChecker::SailChecker(const std::string& config, std::uint64_t reset, std::vector<MemoryRange> backing)
    : backing_(std::move(backing)), reference_(config, reset, backing_) {}
bool SailChecker::backed(std::uint64_t address, std::size_t size) const {
  for (const auto& range : backing_)
    if (address >= range.address && address - range.address < range.size && size <= range.size - (address - range.address)) return true;
  return false;
}
void SailChecker::load(std::uint64_t address, std::span<const std::uint8_t> data) { reference_.load(address, data); }
void SailChecker::external_memory(MemoryRange range) { reference_.external_memory(range); }
void SailChecker::host_write(std::uint64_t sample, std::uint64_t address, std::span<const std::uint8_t> data) {
  // Device effects occur in the DUT only. Device reads are replayed when stepping.
  if (backed(address, data.size())) writes_.push_back({sample, address, {data.begin(), data.end()}});
}
void SailChecker::check(const observation::Record& record) {
  using namespace observation;
  // Capture independent reference pre-state before interpreting reported effects.
  const auto* instruction = std::get_if<Instruction>(&record.event);
  const auto pc = instruction ? instruction->pc : std::get<Interrupt>(record.event).trap.epc;
  const Check require(record);
  const auto backed_range = [this](std::uint64_t address, std::size_t size) { return backed(address, size); };
  VectorCheck vector(reference_, vector_unknown_, record, require, backed_range);
  require(instruction || record.environment.interrupt_boundary, "interrupt outside arbitration boundary");
  const auto xlen = reference_.xlen();
  const auto xmask = xlen == 32 ? UINT32_MAX : UINT64_MAX;
  // Sail owns all configured translation stages. Unsupported PTE writes are
  // rejected by the memory checker when they actually occur.
  require(record.sample >= previous_sample_, "sample order moved backwards");
  while (!writes_.empty() && writes_.front().sample <= record.sample) {
    const auto& write = writes_.front();
    reference_.load(write.address, write.bytes);
    writes_.pop_front();
  }
  StepInputs inputs;
  inputs.time = record.environment.time;
  inputs.interrupt_inputs = record.environment.interrupt_inputs;
  inputs.interrupt_boundary = record.environment.interrupt_boundary;
  inputs.hpm_counters = record.environment.hpm_counters;
  inputs.hpm_overflows = record.environment.hpm_overflows;
  require(record.environment.cycle >= previous_cycle_, "active cycle moved backwards");
  inputs.clock_ticks = record.environment.cycle - previous_cycle_;
  // A software mcycle write replaced, rather than incremented, the previous edge.
  if (previous_cycle_write_) {
    require(inputs.clock_ticks > 0, "cycle write without a subsequent edge");
    --inputs.clock_ticks;
  }
  previous_cycle_ = record.environment.cycle;
  previous_cycle_write_ = false;
  previous_sample_ = record.sample;
  // Validate effects and supply only permitted environmental nondeterminism.
  MemoryCheck memory(vector, require, backed_range, [this](std::uint64_t address, std::size_t size) { return reference_.externally_mutable(address, size); });
  std::map<std::pair<Bank, unsigned>, const observation::RegisterWrite*> registers;
  std::vector<const observation::RegisterWrite*> vector_writes;
  std::optional<std::uint64_t> fp_flags;
  auto accumulated_flags = reference_.csr(1);
  for (const auto& [id, effect] : record.effects) {
    (void)id;
    if (const auto* value = std::get_if<MemoryEffect>(&effect)) {
      memory.add(*value, record, inputs);
    } else if (const auto* value = std::get_if<observation::RegisterWrite>(&effect)) {
      if (value->bank == Bank::Vector) {
        require(value->index < 32 && value->mask, "invalid vector register effect");
        vector_writes.push_back(value);
        continue;
      }
      const auto mask = value->bank == Bank::Integer ? xmask : reference_.flen() == 32 ? UINT32_MAX : UINT64_MAX;
      require((value->bank == Bank::Integer || value->bank == Bank::FloatingPoint) && value->bit_offset == 0 && value->mask == mask && !(value->value & ~mask), "unsupported register effect");
      require(registers.emplace(std::pair{value->bank, unsigned(value->index)}, value).second, "duplicate register effect");
    } else if (const auto* value = std::get_if<CsrUpdate>(&effect);
               value && value->operation == CsrOperation::SetBits && value->address == 1) {
      require(!fp_flags && value->mask == 31 && value->value <= 31, "invalid or duplicate FP flag contribution");
      fp_flags = value->value;
    }
  }
  // Complete only this instruction, including Sail's separate wait-release phase.
  StepResult step;
  try {
    step = reference_.step(inputs);
    const bool wrs = step.instruction == 0x00d00073 || step.instruction == 0x01d00073;
    if (step.waiting && (step.instruction == 0x10500073 || wrs)) {
      // Sail first enters its wait state, then completes the same instruction
      // on a separate call. WRS may retire early even under TW/VTW; a reported
      // trap instead requests timeout so Sail independently checks its cause.
      require(!step.retired && !step.trap && step.writes.empty(), "effects before wait release");
      inputs.wait_release = wrs && std::holds_alternative<Retirement>(record.outcome)
          ? StepInputs::WaitRelease::WrsEarlyWake : StepInputs::WaitRelease::Timeout;
      inputs.clock_ticks = 0;
      inputs.hpm_overflows = 0;
      inputs.device_reads.clear();
      inputs.external_reads.clear();
      auto completed = reference_.step(inputs);
      require(completed.pc == step.pc && completed.privilege_before == step.privilege_before && completed.virtualized_before == step.virtualized_before, "wait release changed instruction boundary");
      completed.instruction = step.instruction;
      completed.instruction_bytes = step.instruction_bytes;
      completed.memory.insert(completed.memory.begin(), step.memory.begin(), step.memory.end());
      step = std::move(completed);
    }
  }
  catch (const std::exception& error) { require(false, std::string("Sail step: ") + error.what()); }
  require(step.pc == pc, "instruction PC");
  if (instruction) require(step.privilege_before == instruction->privilege.mode && step.virtualized_before == instruction->privilege.virtualized, "privilege before instruction");
  require(!step.waiting, "reference did not complete instruction");
  // Compare outcomes and effects without changing the reference post-state.
  vector.validate_execution(step, memory.effects());
  vector.complete(step);
  memory.check_atomic(step);
  if (instruction && instruction->encoding_valid_bytes)
    require(step.instruction && *step.instruction == instruction->encoding && step.instruction_bytes == instruction->instruction_bytes, "instruction encoding/length");
  if (const auto* retired = std::get_if<Retirement>(&record.outcome)) {
    require(step.retired && !step.trap, "expected retirement");
    require(step.next_pc == retired->next_pc && step.privilege_after == retired->privilege.mode && step.virtualized_after == retired->privilege.virtualized, "retirement next PC/privilege");
  } else {
    const auto& trap = std::get<observation::Trap>(record.outcome);
    require(step.trap && step.trap->interrupt == !instruction && !step.retired, "expected trap kind");
    if (!instruction) require(!step.instruction && step.memory.empty(), "interrupt executed an instruction");
    require(step.trap->cause == trap.cause && step.trap->epc == trap.epc && step.trap->tval == trap.tval && step.next_pc == trap.target_pc && step.privilege_after == trap.privilege.mode && step.virtualized_after == trap.privilege.virtualized, "trap details");
  }
  std::size_t expected_registers = 0;
  for (const auto& write : step.writes) {
    if (write.bank == rhodium::cosim::RegisterWrite::Bank::Csr) {
      if (write.index == 0xb00 || (xlen == 32 && write.index == 0xb80)) previous_cycle_write_ = true;
      continue;
    }
    if (write.bank == rhodium::cosim::RegisterWrite::Bank::Vector) continue;
    require(write.bank == rhodium::cosim::RegisterWrite::Bank::Integer ||
            write.bank == rhodium::cosim::RegisterWrite::Bank::FloatingPoint, "unsupported reference register bank");
    const auto bank = write.bank == rhodium::cosim::RegisterWrite::Bank::Integer ? Bank::Integer : Bank::FloatingPoint;
    const auto key = std::pair{bank, write.index};
    const auto name = std::string(bank == Bank::Integer ? "x" : "f") + std::to_string(write.index);
    ++expected_registers;
    require(registers.contains(key), "missing register write " + name);
    require(registers.at(key)->value == word(write.value), "register value " + name);
  }
  require(expected_registers == registers.size(), "extra register effect");
  vector.check_writes(vector_writes);
  accumulated_flags |= fp_flags.value_or(0);
  // Explicit CSR effects from other hook producers remain checkable, but the
  // RV5Stage adapter emits no deterministic CSR-bank snapshots.
  for (const auto& [id, effect] : record.effects) {
    (void)id;
    if (const auto* csr = std::get_if<CsrUpdate>(&effect)) {
      if (csr->operation == CsrOperation::SetBits && csr->address == 1) continue;
      require(csr->operation == CsrOperation::AssignMasked, "unsupported CSR delta operation");
      require((reference_.csr(csr->address) & csr->mask) == (csr->value & csr->mask), "CSR delta " + std::to_string(csr->address));
      if (csr->address == 1 || csr->address == 3)
        accumulated_flags = (accumulated_flags & ~(csr->mask & 31)) | (csr->value & csr->mask & 31);
    }
  }
  if (fp_flags) require(accumulated_flags == reference_.csr(1), "architectural fflags");
  memory.check_bytes(step);
  ++checked_;
}
}
