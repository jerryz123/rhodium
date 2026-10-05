// Checks scalar architectural effects without substituting DUT results into the reference state.
// SPDX-License-Identifier: Apache-2.0
#include "scalar-checker.h"
#include <algorithm>
#include <bit>
#include <sstream>
#include <stdexcept>

namespace rhodium::cosim {
namespace {
using namespace observation;
std::uint64_t word(std::span<const std::uint8_t> bytes) {
  if (bytes.size() > 8) throw std::runtime_error("scalar effect exceeds XLEN");
  std::uint64_t result = 0;
  for (std::size_t i = 0; i != bytes.size(); ++i) result |= std::uint64_t(bytes[i]) << (8 * i);
  return result;
}
std::vector<std::uint8_t> bytes(std::uint64_t value, unsigned size) {
  std::vector<std::uint8_t> result(size);
  for (unsigned i = 0; i != size; ++i) result[i] = value >> (8 * i);
  return result;
}
}
ScalarChecker::ScalarChecker(const std::string& config, std::uint64_t reset, std::vector<MemoryRange> backing)
    : backing_(std::move(backing)), reference_(config, reset, backing_) {}
bool ScalarChecker::backed(std::uint64_t address, std::size_t size) const {
  for (const auto& range : backing_)
    if (address >= range.address && address - range.address < range.size && size <= range.size - (address - range.address)) return true;
  return false;
}
void ScalarChecker::load(std::uint64_t address, std::span<const std::uint8_t> data) { reference_.load(address, data); }
void ScalarChecker::host_write(std::uint64_t sample, std::uint64_t address, std::span<const std::uint8_t> data) {
  // Device effects occur in the DUT only. Device reads are replayed when stepping.
  if (backed(address, data.size())) writes_.push_back({sample, address, {data.begin(), data.end()}});
}
void ScalarChecker::check(const observation::Record& record) {
  using namespace observation;
  const auto* instruction = std::get_if<Instruction>(&record.event);
  const auto pc = instruction ? instruction->pc : std::get<Interrupt>(record.event).trap.epc;
  auto require = [&](bool okay, const std::string& detail) {
    if (!okay) {
      std::ostringstream message;
      message << "cosim mismatch: epoch " << record.id.epoch << " order " << record.id.order
              << " PC 0x" << std::hex << pc << ": " << detail;
      throw std::runtime_error(message.str());
    }
  };
  require(instruction != nullptr, "interrupt comparison is not supported yet");
  require(!instruction->privilege.virtualized, "guest observation is not supported");
  require((reference_.csr(0x180) >> 60) == 0, "translated-memory comparison is not supported yet");
  require(record.sample >= previous_sample_, "sample order moved backwards");
  while (!writes_.empty() && writes_.front().sample <= record.sample) {
    const auto& write = writes_.front();
    reference_.load(write.address, write.bytes);
    writes_.pop_front();
  }
  StepInputs inputs;
  inputs.time = record.environment.time;
  inputs.clock_ticks = record.sample - previous_sample_;
  previous_sample_ = record.sample;
  std::vector<const MemoryEffect*> memory;
  std::map<unsigned, const observation::RegisterWrite*> registers;
  for (const auto& [id, effect] : record.effects) {
    (void)id;
    if (const auto* value = std::get_if<MemoryEffect>(&effect)) {
      require(value->kind == AccessKind::Load || value->kind == AccessKind::Store, "atomic/cache-operation comparison is not supported yet");
      require(value->byte_mask && value->byte_mask <= 255 && ((value->byte_mask & (value->byte_mask + 1)) == 0), "invalid scalar byte mask");
      require(memory.empty() && value->access_id == 0 && value->fragment_offset == 0, "fragmented-memory comparison is not supported yet");
      require(!value->physical_valid || value->physical_address == value->virtual_address, "bare-memory physical address");
      const bool fault = value->result == AccessResult::Fault;
      require(value->result != AccessResult::ScFailure, "SC result on ordinary access");
      require(value->read_valid == (!fault && value->kind == AccessKind::Load) && value->write_valid == (!fault && value->kind == AccessKind::Store), "memory result/data-valid flags");
      require(!fault || std::holds_alternative<observation::Trap>(record.outcome), "memory fault without trap");
      if (fault) {
        const auto& trap = std::get<observation::Trap>(record.outcome);
        const auto cause = value->kind == AccessKind::Load ? 5U : 7U;
        require((trap.cause == cause || trap.cause + 1 == cause) && trap.tval == value->virtual_address, "bare-memory access fault address/cause");
      }
      memory.push_back(value);
      const auto size = std::popcount(value->byte_mask);
      if (value->read_valid && !backed(value->virtual_address, size))
        inputs.device_reads.push_back({value->virtual_address, bytes(value->read_data, size)});
    } else if (const auto* value = std::get_if<observation::RegisterWrite>(&effect)) {
      require(value->bank == Bank::Integer && value->bit_offset == 0 && value->mask == UINT64_MAX, "unsupported register effect");
      require(registers.emplace(value->index, value).second, "duplicate GPR effect");
    }
  }
  StepResult step;
  try {
    step = reference_.step(inputs);
    if (step.waiting && step.instruction == 0x10500073) {
      // Sail first enters its wait state, then completes the same instruction
      // on a separate call. An observed WFI outcome authorizes this permitted
      // implementation-dependent wake, not executing the following instruction.
      require(!step.retired && !step.trap && step.writes.empty(), "effects before WFI wake");
      inputs.wake_wait = true;
      inputs.clock_ticks = 0;
      inputs.device_reads.clear();
      auto completed = reference_.step(inputs);
      require(completed.pc == step.pc && completed.privilege_before == step.privilege_before, "WFI wake changed instruction boundary");
      completed.instruction = step.instruction;
      completed.instruction_bytes = step.instruction_bytes;
      completed.memory.insert(completed.memory.begin(), step.memory.begin(), step.memory.end());
      step = std::move(completed);
    }
  }
  catch (const std::exception& error) { require(false, std::string("Sail step: ") + error.what()); }
  require(step.pc == pc, "instruction PC");
  require(step.privilege_before == instruction->privilege.mode, "privilege before instruction");
  require(!step.waiting, "reference did not complete instruction");
  if (instruction->encoding_valid_bytes)
    require(step.instruction && *step.instruction == instruction->encoding && step.instruction_bytes == instruction->instruction_bytes, "instruction encoding/length");
  if (const auto* retired = std::get_if<Retirement>(&record.outcome)) {
    require(step.retired && !step.trap, "expected retirement");
    require(step.next_pc == retired->next_pc && step.privilege_after == retired->privilege.mode && !retired->privilege.virtualized, "retirement next PC/privilege");
  } else {
    const auto& trap = std::get<observation::Trap>(record.outcome);
    require(step.trap && !step.trap->interrupt && !step.retired, "expected synchronous trap");
    require(!trap.guest_valid && !trap.privilege.virtualized, "guest trap");
    require(step.trap->cause == trap.cause && step.trap->epc == trap.epc && step.trap->tval == trap.tval && step.next_pc == trap.target_pc && step.privilege_after == trap.privilege.mode, "trap details");
  }
  std::size_t expected_registers = 0;
  for (const auto& write : step.writes) {
    if (write.bank == rhodium::cosim::RegisterWrite::Bank::Csr) continue;
    require(write.bank == rhodium::cosim::RegisterWrite::Bank::Integer, "noninteger reference write");
    ++expected_registers;
    require(registers.contains(write.index), "missing GPR write x" + std::to_string(write.index));
    require(registers.at(write.index)->value == word(write.value), "GPR value x" + std::to_string(write.index));
  }
  require(expected_registers == registers.size(), "extra GPR effect");
  for (const auto& [id, effect] : record.effects) {
    (void)id;
    if (const auto* csr = std::get_if<CsrUpdate>(&effect)) {
      require(csr->operation == CsrOperation::AssignMasked, "unsupported CSR delta operation");
      require((reference_.csr(csr->address) & csr->mask) == (csr->value & csr->mask), "CSR delta " + std::to_string(csr->address));
    }
  }
  std::vector<MemoryAccess> accesses;
  for (const auto& access : step.memory) if (!access.instruction) accesses.push_back(access);
  std::size_t next_access = 0;
  for (const auto* effect : memory) {
    for (const bool write : {false, true}) {
      if (!(write ? effect->write_valid : effect->read_valid)) continue;
      require(next_access < accesses.size(), "extra DUT memory effect");
      const auto& access = accesses[next_access++];
      require(access.write == write && access.address == effect->virtual_address && access.value.size() == static_cast<std::size_t>(std::popcount(effect->byte_mask)), "memory address/width/direction");
      require(access.value == bytes(write ? effect->write_data : effect->read_data, access.value.size()), "memory value");
    }
  }
  if (next_access != accesses.size()) {
    std::ostringstream detail;
    detail << "missing DUT memory effect at 0x" << std::hex << accesses[next_access].address << " bytes " << accesses[next_access].value.size();
    require(false, detail.str());
  }
  ++checked_;
}
}
