// Compares physical store outcomes and coordinates MMIO without changing either memory model.
// SPDX-License-Identifier: Apache-2.0
#include "memory.h"
#include <bit>

namespace rhodium::cosim {
using namespace observation;
namespace {
std::vector<std::uint8_t> bytes(std::uint64_t value, unsigned size) {
  std::vector<std::uint8_t> result(size);
  for (unsigned i = 0; i != size; ++i) result[i] = value >> (8 * i);
  return result;
}
}
void MemoryCheck::add(const MemoryEffect& effect, const Record& record, StepInputs& inputs) {
  const auto* value = &effect;
  // Cache maintenance has no architectural byte mutation. Block zero is
  // normalized to stores by the hart adapter and checked like any other write.
  require(value->byte_mask && value->byte_mask <= 255 && ((value->byte_mask & (value->byte_mask + 1)) == 0), "invalid memory fragment byte mask");
  const auto size = static_cast<unsigned>(std::popcount(value->byte_mask));
  if (!memory.empty())
    require(value->kind == memory.front()->kind,
            "inconsistent memory fragment kinds");
  const bool fault = value->result == AccessResult::Fault;
  const bool failed_sc = value->result == AccessResult::ScFailure;
  const bool reads = value->kind == AccessKind::Load || value->kind == AccessKind::Lr || value->kind == AccessKind::Amo;
  const bool writes = value->kind == AccessKind::Store || value->kind == AccessKind::Sc || value->kind == AccessKind::Amo;
  require(!failed_sc || value->kind == AccessKind::Sc, "SC result on non-SC access");
  require(value->read_valid == (!fault && reads) && value->write_valid == (!fault && !failed_sc && writes), "memory result/data-valid flags");
  if (failed_sc) inputs.sc_failure = true;
  require(!fault || vector_.is_fault_first() || std::holds_alternative<observation::Trap>(record.outcome), "memory fault without trap");
  memory.push_back(value);
  const auto address = value->physical_valid ? value->physical_address : value->virtual_address;
  if (value->kind == AccessKind::Amo || value->kind == AccessKind::Lr || value->kind == AccessKind::Sc)
    require(fault || backed(address, size), "device atomics are not qualified");
  if (value->read_valid && !backed(address, size))
    inputs.device_reads.push_back({address, bytes(value->read_data, size)});
}
void MemoryCheck::check_atomic(const StepResult& step) const {
  // A pre-dispatch fault has no memory effect. Sail must independently trap;
  // the ordinary trap comparison below still checks its cause and address.
  if (step.atomic && !(step.trap && memory.empty())) {
    require(memory.size() == 1, "missing atomic attempt");
    const auto& effect = *memory.front();
    const auto kind = step.atomic->kind == AtomicAccess::Kind::Lr ? AccessKind::Lr :
                      step.atomic->kind == AtomicAccess::Kind::Sc ? AccessKind::Sc : AccessKind::Amo;
    require(effect.kind == kind && effect.fragment_offset == 0 && effect.virtual_address == step.atomic->virtual_address &&
            std::popcount(effect.byte_mask) == int(step.atomic->bytes), "atomic kind/address/width");
    require((effect.result == AccessResult::Fault) == step.trap.has_value(), "atomic fault outcome");
    if (step.atomic->physical_address) {
      require(effect.physical_valid || *step.atomic->physical_address == effect.virtual_address, "missing translated SC address");
      require(!effect.physical_valid || effect.physical_address == *step.atomic->physical_address, "SC physical address");
    }
  } else if (!step.atomic) {
    for (const auto* effect : memory)
      require(effect->kind == AccessKind::Load || effect->kind == AccessKind::Store || effect->kind == AccessKind::CacheOperation, "atomic effect on non-atomic instruction");
  }
}
void MemoryCheck::check_bytes(const StepResult& step) const {
  // RAM writes compare the final byte values and touched addresses, not access
  // identities or beat partitioning. MMIO writes retain their ordered bytes;
  // repeated device writes are observable even if their values are identical.
  std::map<std::uint64_t, std::uint8_t> expected, actual;
  std::vector<std::pair<std::uint64_t, std::uint8_t>> expected_device, actual_device;
  for (const auto& access : step.memory) {
    if (access.page_table) require(!access.write, "hardware A/D updates are not qualified");
    else if (access.write) {
      for (std::size_t i = 0; i != access.value.size(); ++i) {
        if (access.device) expected_device.emplace_back(access.address+i, access.value[i]);
        else expected[access.address+i] = access.value[i];
      }
    }
  }
  for (const auto* effect : memory) {
    if (!effect->write_valid) continue;
    const auto address = effect->physical_valid ? effect->physical_address : effect->virtual_address;
    const auto size = unsigned(std::popcount(effect->byte_mask));
    const bool device = !backed(address, size);
    for (unsigned i = 0; i != size; ++i) {
      const auto value = std::uint8_t(effect->write_data >> (8*i));
      if (device) actual_device.emplace_back(address+i, value);
      else actual[address+i] = value;
    }
  }
  require(actual == expected, "physical store bytes");
  require(actual_device == expected_device, "device store bytes/order");
}
}
