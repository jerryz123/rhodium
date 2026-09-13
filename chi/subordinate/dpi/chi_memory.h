// Defines bounded byte storage shared by clocked CHI DPI and simulator image loading.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace rhodium::chi {

class Memory {
 public:
  explicit Memory(std::uint64_t size);
  std::uint64_t size() const { return size_; }
  void read(std::uint64_t offset, std::span<std::uint8_t> bytes) const;
  void write(std::uint64_t offset, std::span<const std::uint8_t> bytes);
  void zero(std::uint64_t offset, std::size_t length);

 private:
  void check(std::uint64_t offset, std::size_t length) const;
  static constexpr std::size_t page_size = 4096;
  using Page = std::array<std::uint8_t, page_size>;
  std::uint64_t size_;
  std::unordered_map<std::uint64_t, Page> pages_;
};

// Repeated access to an identity preserves bytes; inconsistent capacities fail.
Memory& memory(std::uint32_t model_id, std::uint64_t size);

struct RegisteredMemory {
  std::uint32_t model_id;
  std::uint64_t base;
  std::string owner;
  Memory* storage;
};

// Reset-time registration is idempotent for the same DPI instance and window.
void register_memory(std::uint32_t model_id, std::uint64_t base,
                     std::uint64_t size, const std::string& owner);
Memory& initialized_memory(std::uint32_t model_id, std::uint64_t size);
// Close discovery before image loading. Any initialization error remains fatal.
const std::vector<RegisteredMemory>& freeze_memory_registrations();

}  // namespace rhodium::chi
