// Implements sparse bounded byte memories with allocation-free clearing of empty ranges.
#include "chi_memory.h"

#include <algorithm>
#include <stdexcept>

namespace rhodium::chi {

namespace {
struct Registrations {
  std::vector<RegisteredMemory> memories;
  bool frozen = false;
  std::string error;
};
Registrations& registrations() {
  static Registrations registry;
  return registry;
}
}  // namespace

void register_memory(std::uint32_t model_id, std::uint64_t base,
                     std::uint64_t size, const std::string& owner) {
  auto& registry = registrations();
  try {
    if (!registry.error.empty()) throw std::runtime_error(registry.error);
    if (owner.empty() || size == 0 || base > UINT64_MAX - (size - 1))
      throw std::invalid_argument("invalid CHI image memory identity or range");
    for (const auto& entry : registry.memories) {
      if (entry.model_id == model_id) {
        if (entry.owner != owner || entry.base != base || entry.storage->size() != size)
          throw std::invalid_argument("conflicting CHI memory model ID or window");
        return;
      }
      if (entry.owner == owner)
        throw std::invalid_argument("CHI memory instance changed model ID");
      if (base <= entry.base + entry.storage->size() - 1 && entry.base <= base + size - 1)
        throw std::invalid_argument("overlapping CHI image memory windows");
    }
    if (registry.frozen) throw std::logic_error("CHI memory registration after loading started");
    registry.memories.push_back({model_id, base, owner, &memory(model_id, size)});
  } catch (const std::exception& error) {
    registry.error = error.what();
    throw;
  }
}

Memory& initialized_memory(std::uint32_t model_id, std::uint64_t size) {
  const auto& registry = registrations();
  if (!registry.error.empty()) throw std::runtime_error(registry.error);
  for (const auto& entry : registry.memories)
    if (entry.model_id == model_id && entry.storage->size() == size) return *entry.storage;
  throw std::logic_error("CHI memory accessed before initialization or with inconsistent capacity");
}

const std::vector<RegisteredMemory>& freeze_memory_registrations() {
  auto& registry = registrations();
  if (!registry.error.empty()) throw std::runtime_error(registry.error);
  registry.frozen = true;
  return registry.memories;
}

Memory::Memory(std::uint64_t size) : size_(size) {
  if (size == 0) throw std::invalid_argument("CHI memory capacity must be positive");
}

void Memory::check(std::uint64_t offset, std::size_t length) const {
  if (offset > size_ || length > size_ - offset)
    throw std::out_of_range("CHI memory access exceeds capacity");
}

void Memory::read(std::uint64_t offset, std::span<std::uint8_t> bytes) const {
  check(offset, bytes.size());
  while (!bytes.empty()) {
    const auto index = offset / page_size;
    const auto start = static_cast<std::size_t>(offset % page_size);
    const auto count = std::min(bytes.size(), page_size - start);
    const auto found = pages_.find(index);
    if (found == pages_.end()) std::fill_n(bytes.data(), count, 0);
    else std::copy_n(found->second.data() + start, count, bytes.data());
    bytes = bytes.subspan(count);
    offset += count;
  }
}

void Memory::write(std::uint64_t offset, std::span<const std::uint8_t> bytes) {
  check(offset, bytes.size());
  while (!bytes.empty()) {
    const auto start = static_cast<std::size_t>(offset % page_size);
    const auto count = std::min(bytes.size(), page_size - start);
    std::copy_n(bytes.data(), count, pages_[offset / page_size].data() + start);
    bytes = bytes.subspan(count);
    offset += count;
  }
}

void Memory::zero(std::uint64_t offset, std::size_t length) {
  check(offset, length);
  if (length == 0) return;
  const auto last = offset + length - 1;
  for (auto it = pages_.begin(); it != pages_.end();) {
    const auto page = it->first * page_size;
    if (it->first < offset / page_size || it->first > last / page_size) { ++it; continue; }
    const auto begin = offset > page ? static_cast<std::size_t>(offset - page) : 0;
    const auto end = it->first == last / page_size ? static_cast<std::size_t>(last % page_size) + 1 : page_size;
    if (begin == 0 && end == page_size) it = pages_.erase(it);
    else { std::fill(it->second.begin() + begin, it->second.begin() + end, 0); ++it; }
  }
}

Memory& memory(std::uint32_t model_id, std::uint64_t size) {
  static std::unordered_map<std::uint32_t, Memory> memories;
  auto [it, inserted] = memories.try_emplace(model_id, size);
  if (!inserted && it->second.size() != size)
    throw std::invalid_argument("CHI memory identity has inconsistent capacity");
  return it->second;
}

}  // namespace rhodium::chi
