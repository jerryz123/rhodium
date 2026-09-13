// Maps initial-image physical ranges to protocol-independent native byte-store callbacks.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace rhodium::fesvr {

struct ImageMemoryRegion {
  std::string name;
  std::uint64_t base, size, storage_size, offset;
  std::function<void(std::uint64_t, std::span<std::uint8_t>)> read;
  std::function<void(std::uint64_t, std::span<const std::uint8_t>)> write;
  std::function<void(std::uint64_t, std::size_t)> zero;
};

class ImageMemoryMap {
 public:
  struct Segment {
    const ImageMemoryRegion* region;
    std::uint64_t offset;
    std::size_t length;
  };

  void add(ImageMemoryRegion region) {
    if (frozen_) throw std::logic_error("image memory registrations are frozen");
    if (region.name.empty() || region.size == 0 ||
        region.base > UINT64_MAX - (region.size - 1) ||
        region.offset > region.storage_size || region.size > region.storage_size - region.offset ||
        !region.read || !region.write || !region.zero)
      throw std::invalid_argument("invalid image memory region");
    for (const auto& other : regions_) {
      if (region.name == other.name ||
          (region.base <= other.base + other.size - 1 && other.base <= region.base + region.size - 1))
        throw std::invalid_argument("duplicate or overlapping image memory regions");
    }
    regions_.push_back(std::move(region));
    std::sort(regions_.begin(), regions_.end(), [](const auto& a, const auto& b) { return a.base < b.base; });
  }

  void freeze() { frozen_ = true; }
  bool empty() const { return regions_.empty(); }

  Segment segment(std::uint64_t address, std::size_t length) const {
    for (const auto& region : regions_) {
      if (address < region.base)
        return {nullptr, 0, static_cast<std::size_t>(std::min<std::uint64_t>(length, region.base - address))};
      const auto relative = address - region.base;
      if (relative < region.size)
        return {&region, region.offset + relative,
                static_cast<std::size_t>(std::min<std::uint64_t>(length, region.size - relative))};
    }
    return {nullptr, 0, length};
  }

 private:
  bool frozen_ = false;
  std::vector<ImageMemoryRegion> regions_;
};

}  // namespace rhodium::fesvr
