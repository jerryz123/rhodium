// Checks physical store outcomes and supplies device reads without reference access instrumentation.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "vector.h"
#include <utility>

namespace rhodium::cosim {
// Per-record scalar/atomic/vector memory normalization, with device replay inputs kept explicit.
class MemoryCheck {
 public:
  MemoryCheck(VectorCheck& vector, Check check, BackingCheck backing)
      : vector_(vector), require(std::move(check)), backed(std::move(backing)) {}
  void add(const observation::MemoryEffect& effect, const observation::Record& record, StepInputs& inputs);
  void check_atomic(const StepResult& step) const;
  void check_bytes(const StepResult& step) const;
  const std::vector<const observation::MemoryEffect*>& effects() const { return memory; }
 private:
  VectorCheck& vector_;
  Check require;
  BackingCheck backed;
  std::vector<const observation::MemoryEffect*> memory;
};
}
