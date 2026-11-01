// Implements checked sampled counts and deterministic hierarchical-name JSON reporting.
// SPDX-License-Identifier: Apache-2.0
#include "collector.h"
#include <limits>
#include <stdexcept>

namespace rhodium::statistics {
Collector::Collector(std::span<const std::string_view> descriptors)
    : descriptors_(descriptors.begin(), descriptors.end()), counts_(descriptors.size()), seen_(descriptors.size()) {}

void Collector::begin(bool reset) {
  if (sampling_) throw std::runtime_error("statistics sample already open");
  if (sample_ == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("statistics sample overflow");
  ++sample_;
  sampling_ = true;
  reset_ = reset;
}

void Collector::count(std::uint32_t id) {
  if (!sampling_ || id >= counts_.size()) throw std::runtime_error("invalid statistics callback");
  if (seen_[id] == sample_) throw std::runtime_error("duplicate statistic sample on one edge");
  seen_[id] = sample_;
  if (reset_ || frozen_) return;
  if (counts_[id] == std::numeric_limits<std::uint64_t>::max()) throw std::overflow_error("statistic count overflow");
  ++counts_[id];
}

void Collector::end() {
  if (!sampling_) throw std::runtime_error("statistics sample not open");
  sampling_ = false;
}

void Collector::freeze() {
  if (sampling_) throw std::runtime_error("statistics freeze before edge settled");
  frozen_ = true;
}

std::string Collector::json() const {
  if (sampling_ || !frozen_) throw std::runtime_error("statistics report before completion");
  std::string result = "{";
  for (std::size_t id = 0; id < counts_.size(); ++id) {
    if (id) result += ',';
    result += descriptors_[id] + std::to_string(counts_[id]);
  }
  return result + '}';
}
}
