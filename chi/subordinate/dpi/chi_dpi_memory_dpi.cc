// Translates clocked CHI beats to the shared bounded native byte store.
// SPDX-License-Identifier: Apache-2.0
#include "chi_dpi_memory_dpi.h"
#include "chi_memory.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <cstdio>

namespace {

constexpr std::size_t kDpiDataWords = 512 / 32;
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

unsigned char rhodium_chi_memory_init(int model_id, long long capacity, long long base_address) {
  try {
    const auto scope = svGetScope();
    const char* owner = scope ? svGetNameFromScope(scope) : nullptr;
    rhodium::chi::register_memory(static_cast<std::uint32_t>(model_id),
                                  static_cast<std::uint64_t>(base_address),
                                  static_cast<std::uint64_t>(capacity), owner ? owner : "");
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "CHI memory initialization failed: %s\n", error.what());
    return 3;
  }
}

unsigned char rhodium_chi_memory_access(int model_id,
                                        long long capacity,
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
  const auto enabled_bytes = static_cast<std::uint64_t>(write_mask);
  try {
    auto& memory = rhodium::chi::initialized_memory(static_cast<std::uint32_t>(model_id),
                                       static_cast<std::uint64_t>(capacity));
    std::array<std::uint8_t, 64> bytes{};
    auto beat = std::span(bytes).first(unsigned_beat_bytes);
    // Read first so partial writes preserve disabled bytes and validate the full beat.
    memory.read(beat_address, beat);
    if (write != 0) {
      for (std::size_t lane = 0; lane < unsigned_beat_bytes; ++lane) {
        if (((enabled_bytes >> lane) & 1U) != 0) bytes[lane] = packed_byte(write_data, lane);
      }
      memory.write(beat_address, beat);
    } else {
      for (std::size_t lane = 0; lane < unsigned_beat_bytes; ++lane)
        set_packed_byte(read_data, lane, bytes[lane]);
    }
  } catch (const std::exception&) {
    return 3;
  }
  return 0;
}
