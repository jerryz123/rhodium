// Compares scalar/vector DUT records with independent Sail and tracks unspecified vector bits.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "../events/record.h"
#include "reference.h"
#include <deque>

namespace rhodium::cosim {
class SailChecker {
 public:
  SailChecker(const std::string& configuration, std::uint64_t reset_pc,
                std::vector<MemoryRange> backing);
  void load(std::uint64_t address, std::span<const std::uint8_t> bytes);
  void external_memory(MemoryRange range);
  void host_write(std::uint64_t sample, std::uint64_t address, std::span<const std::uint8_t> bytes);
  void check(const observation::Record& record);
  std::uint64_t checked() const { return checked_; }
 private:
  bool backed(std::uint64_t address, std::size_t size) const;
  struct Write { std::uint64_t sample, address; std::vector<std::uint8_t> bytes; };
  std::vector<MemoryRange> backing_;
  SailReference reference_;
  std::deque<Write> writes_;
  // Legal partial-load writes may diverge from Sail. Never repair its state or
  // silently consume divergent bits; a checked overwrite makes them comparable.
  std::map<unsigned, std::vector<std::uint8_t>> vector_unknown_;
  std::uint64_t checked_ = 0, previous_sample_ = 0;
  std::uint64_t previous_cycle_ = 0;
  bool previous_cycle_write_ = false;
};
}
