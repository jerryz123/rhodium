// Compares vector architectural post-state, including permitted partial-load divergence.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "diagnostic.h"
#include "reference.h"
#include <functional>

namespace rhodium::cosim {
using VectorRegisters = std::map<unsigned, std::vector<std::uint8_t>>;
using BackingCheck = std::function<bool(std::uint64_t, std::size_t)>;

// Reconstructs DUT post-state without copying it into the independent reference.
class VectorCheck {
 public:
  VectorCheck(SailReference& reference, VectorRegisters& unknown, const observation::Record& record,
              Check check, BackingCheck backing);
  bool is_memory() const { return vector_memory; }
  bool is_fault_first() const { return fault_only_first; }
  void validate_execution(const StepResult& step, const std::vector<const observation::MemoryEffect*>& memory);
  void complete(const StepResult& step);
  void check_writes(const std::vector<const observation::RegisterWrite*>& vector_writes);
 private:
  SailReference& reference_;
  VectorRegisters& vector_unknown_;
  Check require;
  BackingCheck backed;
  std::uint64_t encoding, opcode, vector_type, vector_length, vector_start, memory_length;
  unsigned addressing, subop, fields, index_bytes, memory_eew, field_registers;
  bool vector_memory, whole_memory, mask_memory, indexed, fault_only_first, usable_vector_type, vector_compute;
  std::vector<std::uint8_t> vector_mask;
  VectorRegisters before, optional;
};
}
