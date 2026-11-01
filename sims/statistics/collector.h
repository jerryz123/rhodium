// Accumulates per-site sampled counts without coupling the collector to an HDL simulator.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rhodium::statistics {
class Collector {
 public:
  explicit Collector(std::span<const std::string_view> descriptors);
  void begin(bool reset);
  void count(std::uint32_t id);
  void end();
  void freeze();
  std::string json() const;
 private:
  std::vector<std::string> descriptors_;
  std::vector<std::uint64_t> counts_;
  std::vector<std::uint64_t> seen_;
  std::uint64_t sample_ = 0;
  bool sampling_ = false, reset_ = true, frozen_ = false;
};
}
