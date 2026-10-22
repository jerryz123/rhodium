// Checks valid circular indices against modulo arithmetic and cross-limb boundaries.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <array>
#include <cstdint>

struct Case { unsigned width, capacity; };
constexpr std::array cases{Case{1, 1}, Case{1, 2}, Case{2, 3}, Case{2, 4},
    Case{3, 5}, Case{3, 8}, Case{4, 9}, Case{4, 16}, Case{8, 17},
    Case{8, 255}, Case{8, 256}, Case{16, 257}, Case{16, 65536}};
using uint128 = unsigned __int128;

template <typename Word> uint128 low_128(const Word &value) {
  uint128 result = 0;
  for (unsigned word = 0; word < 4 && word < value.words.size(); ++word)
    result |= uint128(value.words[word]) << (32 * word);
  return result;
}

void check_wide() {
  eval();
  const uint128 value65 = std::uint64_t(low_128(ports::index)) |
                         (uint128(ports::index.words[2] & 1u) << 64);
  const uint128 capacity65 = (uint128(1) << 64) + 1;
  if (value65 < capacity65)
    CHECK(low_128(next_65) == (value65 + 1) % capacity65);

  const uint128 value128 = low_128(ports::index);
  if (ports::index.words[4] == 0) {
    CHECK(low_128(next_129) == value128 + 1);
    CHECK(next_129.words[4] == (value128 == ~uint128(0)));
  } else if (ports::index.words[4] == 1 && value128 == 0) {
    CHECK(low_128(next_129) == 0 && next_129.words[4] == 0);
  }
}

int main() {
  return run_test([] {
    for (unsigned value = 0; value < 65536; ++value) {
      ports::index.words = {};
      ports::index.words[0] = value;
      check_wide();
      for (unsigned c = 0; c < cases.size(); ++c) {
        const auto [width, capacity] = cases[c];
        const unsigned selected = value & ((1u << width) - 1);
        if (selected < capacity)
          CHECK(small_next[c] == (selected + 1) % capacity);
      }
    }

    // Carry at each 32-bit limb and wrap at the 65/129-bit capacities' final index.
    for (unsigned limbs = 1; limbs <= 4; ++limbs) {
      ports::index.words = {};
      for (unsigned word = 0; word < limbs; ++word)
        ports::index.words[word] = UINT32_MAX;
      check_wide();
      ports::index.words = {};
      ports::index.words[limbs] = 1;
      check_wide();
    }
  });
}
