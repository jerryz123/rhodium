// Compares complete scalar DUT records with independent Sail execution and ordered host writes.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "observation.h"
#include "sail-reference.h"
#include <deque>

namespace rhodium::cosim {
class ScalarChecker {
 public:
  ScalarChecker(const std::string& configuration, std::uint64_t reset_pc,
                std::vector<MemoryRange> backing);
  void load(std::uint64_t address, std::span<const std::uint8_t> bytes);
  void host_write(std::uint64_t sample, std::uint64_t address, std::span<const std::uint8_t> bytes);
  void check(const observation::Record& record);
  std::uint64_t checked() const { return checked_; }
 private:
  bool backed(std::uint64_t address, std::size_t size) const;
  struct Write { std::uint64_t sample, address; std::vector<std::uint8_t> bytes; };
  std::vector<MemoryRange> backing_;
  SailReference reference_;
  std::deque<Write> writes_;
  std::uint64_t checked_ = 0, previous_sample_ = 0;
};
}
