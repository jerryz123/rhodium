// Checks vector post-state using architectural cursors, not reference access-attempt instrumentation.
// SPDX-License-Identifier: Apache-2.0
#include "vector.h"
#include <algorithm>
#include <bit>
#include <utility>

namespace rhodium::cosim {
using namespace observation;
VectorCheck::VectorCheck(SailReference& reference, VectorRegisters& unknown,
                         const Record& record, Check check, BackingCheck backing)
    : reference_(reference), vector_unknown_(unknown), require(std::move(check)), backed(std::move(backing)) {
  const auto* instruction = std::get_if<Instruction>(&record.event);
  encoding = instruction ? instruction->encoding : 0;
  opcode = encoding & 127;
  const auto form = (encoding >> 12) & 7;
  vector_memory = (opcode == 7 || opcode == 39) && (form == 0 || form >= 5);
  vector_compute = opcode == 0x57 && form != 7;
  addressing = (encoding >> 26) & 3;
  subop = (encoding >> 20) & 31;
  whole_memory = vector_memory && addressing == 0 && subop == 8;
  mask_memory = vector_memory && addressing == 0 && subop == 11;
  fields = vector_memory && !whole_memory ? 1 + (encoding >> 29) : 1;
  indexed = vector_memory && (addressing & 1);
  fault_only_first = vector_memory && opcode == 7 && addressing == 0 && subop == 16;
  vector_type = reference_.csr(0xc21);
  vector_length = reference_.csr(0xc20);
  vector_start = reference_.csr(8);
  index_bytes = 1U << (form & 3);
  memory_eew = indexed ? 1U << ((vector_type >> 3) & 7) : index_bytes;
  const int lmul = int(vector_type & 3) - int(vector_type & 4);
  field_registers = 1U << std::max(0, lmul + int(std::countr_zero(memory_eew)) - int((vector_type >> 3) & 7));
  vector_mask = reference_.vector_register(0);
  memory_length = whole_memory ? (1 + (encoding >> 29)) * vector_mask.size() / memory_eew :
      mask_memory ? (vector_length + 7) / 8 : vector_length;
  usable_vector_type = whole_memory || !(vector_type >> (reference_.xlen() - 1));
  if (vector_compute || vector_memory)
    for (unsigned reg = 0; reg != 32; ++reg) {
      auto value = reference_.vector_register(reg);
      const auto found = vector_unknown_.find(reg);
      if (found != vector_unknown_.end())
        for (unsigned byte = 0; byte != value.size(); ++byte) value[byte] ^= found->second.at(byte);
      before.emplace(reg, std::move(value));
    }
}
void VectorCheck::validate_execution(const StepResult& step, const std::vector<const MemoryEffect*>& memory) {
  // Sail owns legality. Reserved encodings and invalid register groups have no
  // execution geometry to validate; their trap and unchanged VRF still compare.
  if (step.trap && step.trap->cause == 2) return;
  if (vector_memory) {
    require(!(encoding & 0x10000000U) && (addressing != 0 || !subop || fault_only_first || whole_memory || mask_memory), "unsupported vector memory form");
    for (const auto* effect : memory)
      require(effect->result == AccessResult::Fault || backed(effect->physical_valid ? effect->physical_address : effect->virtual_address, std::popcount(effect->byte_mask)), "vector device memory is not qualified");
  }
  const auto known = [&](unsigned reg, unsigned byte, unsigned mask) {
    const auto found = vector_unknown_.find(reg);
    return found == vector_unknown_.end() || !(found->second.at(byte) & mask);
  };
  if (vector_memory && usable_vector_type) {
    require(memory_eew <= 8 && !vector_mask.empty(), "unsupported vector memory geometry");
    for (auto element = vector_start; element < memory_length; ++element) {
      if (!((encoding >> 25) & 1)) {
        require(known(0, element / 8, 1U << (element % 8)), "unspecified vector memory predicate");
        if (!(vector_mask.at(element / 8) & (1U << (element % 8)))) continue;
      }
      if (indexed) for (unsigned byte = 0; byte != index_bytes; ++byte) {
        const auto position = element * index_bytes + byte;
        const auto reg = ((encoding >> 20) & 31) + position / vector_mask.size();
        require(reg < 32, "vector index group extent");
        require(known(reg, position % vector_mask.size(), 255), "unspecified vector index bits");
      }
      if (opcode == 39) for (unsigned field = 0; field != fields; ++field)
        for (unsigned byte = 0; byte != memory_eew; ++byte) {
          const auto position = element * memory_eew + byte;
          const auto reg = ((encoding >> 7) & 31) + field * field_registers + position / vector_mask.size();
          require(reg < 32, "vector memory register extent");
          require(known(reg, position % vector_mask.size(), 255), "unspecified vector store data");
        }
    }
  }
  if (vector_compute) for (const auto& [reg, unknown] : vector_unknown_) {
    (void)reg;
    require(std::ranges::all_of(unknown, [](auto bits) { return bits == 0; }),
            "vector compute cannot consume divergent partial-load state");
  }
}
void VectorCheck::complete(const StepResult& step) {
  if (!vector_memory || opcode != 7 || !usable_vector_type) return;
  const bool shortened = fault_only_first && !step.trap && reference_.csr(0xc20) < vector_length;
  const bool load_fault = step.trap && (step.trap->cause == 4 || step.trap->cause == 5 || step.trap->cause == 13 || step.trap->cause == 21);
  if (!shortened && !load_fault) return;
  const auto end = reference_.csr(shortened ? 0xc20 : 8);
  require(end >= vector_start && end < memory_length, "vector fault/truncation cursor");
  // Segment faults permit a subset of fields in the faulting element. FOF
  // additionally permits destination updates at/past the shortened VL. These
  // architectural freedoms need no observation of Sail's individual attempts.
  for (auto element = end; element < memory_length; ++element) {
    if (!(shortened || (fields > 1 && element == end))) continue;
    if (!((encoding >> 25) & 1) && !(vector_mask.at(element / 8) & (1U << (element % 8)))) continue;
    for (unsigned field = 0; field != fields; ++field) for (unsigned byte = 0; byte != memory_eew; ++byte) {
      const auto position = element * memory_eew + byte;
      const auto reg = ((encoding >> 7) & 31) + field * field_registers + position / vector_mask.size();
      require(reg < 32, "vector memory register extent");
      auto& mask = optional[reg];
      mask.resize(vector_mask.size());
      mask[position % vector_mask.size()] = 255;
    }
  }
}
void VectorCheck::check_writes(const std::vector<const observation::RegisterWrite*>& writes) {
  VectorRegisters written;
  for (const auto* write : writes) {
    require(before.contains(write->index), "unexpected vector destination");
    auto& actual = before.at(write->index);
    auto& coverage = written[write->index];
    coverage.resize(actual.size());
    for (unsigned bit = 0; bit != 64; ++bit) {
      if (!(write->mask & (std::uint64_t{1} << bit))) continue;
      const auto offset = write->bit_offset + bit;
      require(offset / 8 < actual.size(), "vector fragment extent");
      const auto flag = 1U << (offset % 8);
      require(!(coverage[offset / 8] & flag), "overlapping vector write bit");
      coverage[offset / 8] |= flag;
      actual[offset / 8] = (actual[offset / 8] & ~flag) | ((write->value & (std::uint64_t{1} << bit)) ? flag : 0);
    }
  }
  for (const auto& [reg, actual] : before) {
    const auto expected = reference_.vector_register(reg);
    auto& unknown = vector_unknown_[reg];
    auto& allowed = optional[reg];
    auto& changed = written[reg];
    unknown.resize(actual.size()); allowed.resize(actual.size()); changed.resize(actual.size());
    for (unsigned byte = 0; byte != actual.size(); ++byte) {
      const auto difference = actual[byte] ^ expected[byte];
      require(!(difference & ~(allowed[byte] | (unknown[byte] & ~changed[byte]))), "vector post-state v" + std::to_string(reg));
      unknown[byte] = difference;
    }
  }
}
}
