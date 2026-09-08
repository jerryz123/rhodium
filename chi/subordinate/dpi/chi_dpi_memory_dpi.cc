// Stores native CHI memory beats in sparse fixed-size memory blocks.
#include "chi_dpi_memory_dpi.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace {

constexpr std::size_t kDpiDataWords = 512 / 32;
constexpr std::size_t kMemoryBlockBytes = 64;

using MemoryBlock = std::array<std::uint8_t, kMemoryBlockBytes>;
using SparseMemory = std::unordered_map<std::uint64_t, MemoryBlock>;

std::unordered_map<std::uint32_t, SparseMemory> memories;

bool supported_beat_bytes(std::uint8_t beat_bytes) {
  return beat_bytes == 16 || beat_bytes == 32 || beat_bytes == 64;
}

std::uint8_t packed_byte(const svBitVecVal* value, std::size_t index) {
  const std::size_t word = index / 4;
  const std::size_t shift = (index % 4) * 8;
  return static_cast<std::uint8_t>(value[word] >> shift);
}

void set_packed_byte(svBitVecVal* value,
                     std::size_t index,
                     std::uint8_t byte) {
  const std::size_t word = index / 4;
  const std::size_t shift = (index % 4) * 8;
  value[word] |= static_cast<svBitVecVal>(byte) << shift;
}

}  // namespace

unsigned char rhodium_chi_memory_access(int model_id,
                                        unsigned char beat_bytes,
                                        unsigned char write,
                                        long long address,
                                        const svBitVecVal* write_data,
                                        long long write_mask,
                                        svBitVecVal* read_data) {
  if (write_data == nullptr || read_data == nullptr) {
    return 2;
  }
  for (std::size_t word = 0; word < kDpiDataWords; ++word) {
    read_data[word] = 0;
  }
  if (!supported_beat_bytes(beat_bytes)) {
    return 1;
  }

  const auto unsigned_beat_bytes = static_cast<std::uint8_t>(beat_bytes);
  const auto unsigned_address = static_cast<std::uint64_t>(address);
  const auto beat_address = unsigned_address &
      ~(static_cast<std::uint64_t>(unsigned_beat_bytes) - 1);
  const auto block_address = beat_address &
      ~(static_cast<std::uint64_t>(kMemoryBlockBytes) - 1);
  const auto block_offset = static_cast<std::size_t>(beat_address - block_address);
  const auto enabled_bytes = static_cast<std::uint64_t>(write_mask);
  auto& memory = memories[static_cast<std::uint32_t>(model_id)];

  if (write != 0) {
    auto& block = memory[block_address];
    for (std::size_t lane = 0; lane < unsigned_beat_bytes; ++lane) {
      if (((enabled_bytes >> lane) & 1U) != 0) {
        block[block_offset + lane] = packed_byte(write_data, lane);
      }
    }
  } else {
    const auto found = memory.find(block_address);
    if (found != memory.end()) {
      for (std::size_t lane = 0; lane < unsigned_beat_bytes; ++lane) {
        set_packed_byte(read_data, lane, found->second[block_offset + lane]);
      }
    }
  }
  return 0;
}
