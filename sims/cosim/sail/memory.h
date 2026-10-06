// Checks physical stores and supplies separately scoped device and external RAM reads.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "vector.h"
#include <utility>

namespace rhodium::cosim {
// Per-record memory normalization with separate device and external RAM read inputs.
class MemoryCheck {
 public:
  MemoryCheck(VectorCheck& vector, Check check, BackingCheck backing, BackingCheck external)
      : vector_(vector), require(std::move(check)), backed(std::move(backing)), externally_mutable(std::move(external)) {}
  void add(const observation::MemoryEffect& effect, const observation::Record& record, StepInputs& inputs);
  void check_atomic(const StepResult& step) const;
  void check_bytes(const StepResult& step) const;
  const std::vector<const observation::MemoryEffect*>& effects() const { return memory; }
 private:
  VectorCheck& vector_;
  Check require;
  BackingCheck backed;
  BackingCheck externally_mutable;
  std::vector<const observation::MemoryEffect*> memory;
};
}
