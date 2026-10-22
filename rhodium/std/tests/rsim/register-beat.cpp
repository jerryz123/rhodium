// Compares register-beat projection with independent bit-position arithmetic.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <array>
#include <cstdint>

struct Case {
  unsigned bus_bytes, register_bits, enable_count;
};
constexpr std::array cases{
    Case{1, 1, 1}, Case{1, 8, 1}, Case{3, 16, 2}, Case{3, 24, 3},
    Case{8, 32, 4}, Case{8, 64, 8}, Case{16, 8, 1}, Case{16, 32, 4},
    Case{16, 64, 8}, Case{16, 128, 16}, Case{32, 256, 32}, Case{64, 512, 64}};

std::uint64_t random_state = UINT64_C(0x631a2b7d8f4905ec);
std::uint64_t random_word() {
  random_state ^= random_state << 13;
  random_state ^= random_state >> 7;
  random_state ^= random_state << 17;
  return random_state;
}

template <typename Word> bool bit(const Word &value, unsigned position) {
  return (value.words[position / 32] >> (position % 32)) & 1u;
}

int main() {
  return run_test([] {
    for (unsigned sample = 0; sample < 32; ++sample) {
      for (unsigned word = 0; word < 16; ++word) {
        write_bus.words[word] = sample == 0 ? 0 : sample == 1 ? UINT32_MAX : random_word();
        read_register.words[word] = sample == 0 ? UINT32_MAX : sample == 1 ? 0 : random_word();
      }
      // Exhaust the low three enable bits; wider beats also see random enables.
      byte_enables = (random_word() & ~UINT64_C(7)) | (sample % 8);
      for (unsigned lane = 0; lane < 256; ++lane) {
        register_lane = lane;
        eval();
        for (unsigned c = 0; c < cases.size(); ++c) {
          const auto [bus_bytes, register_bits, enable_count] = cases[c];
          for (unsigned word = 0; word < 16; ++word) {
            std::uint32_t extracted = 0, full_extracted = 0, placed = 0;
            for (unsigned b = 0; b < 32; ++b) {
              const unsigned pos = word * 32 + b;
              if (pos + lane * 8 < bus_bytes * 8 && bit(write_bus, pos + lane * 8)) {
                full_extracted |= UINT32_C(1) << b;
                if (pos < register_bits)
                  extracted |= UINT32_C(1) << b;
              }
              if (pos < bus_bytes * 8 && pos >= lane * 8 &&
                  pos - lane * 8 < register_bits && bit(read_register, pos - lane * 8))
                placed |= UINT32_C(1) << b;
            }
            CHECK(write_values[c].words[word] == extracted);
            CHECK(full_write_values[c].words[word] == full_extracted);
            CHECK(read_beats[c].words[word] == placed);
          }
          std::uint64_t full_mask = 0, narrow_mask = 0;
          for (unsigned pos = 0; pos < 64; ++pos) {
            if (pos + lane < bus_bytes && ((byte_enables >> (pos + lane)) & 1u)) {
              full_mask |= UINT64_C(1) << pos;
              if (pos < enable_count)
                narrow_mask |= UINT64_C(1) << pos;
            }
          }
          CHECK(full_enables[c] == full_mask);
          CHECK(register_enables[c] == narrow_mask);
        }
      }
    }
  });
}
