// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <array>
#include <cstdint>

struct Case { unsigned width, limit; };
constexpr std::array cases{Case{1, 0}, Case{1, 1}, Case{2, 2}, Case{2, 3},
    Case{3, 6}, Case{3, 7}, Case{4, 8}, Case{4, 15}, Case{8, 17},
    Case{8, 254}, Case{8, 255}, Case{16, 257}, Case{16, 65535}};
using Words = std::array<std::uint32_t, 5>;

bool less_equal(const Words &a, const Words &b) {
  for (unsigned i = a.size(); i-- > 0;) {
    if (a[i] != b[i]) return a[i] < b[i];
  }
  return true;
}

Words successor(Words a) {
  for (auto &word : a) {
    if (++word != 0) break;
  }
  return a;
}

Words predecessor(Words a) {
  if (a == Words{}) return a;
  for (auto &word : a) {
    if (word-- != 0) break;
  }
  return a;
}

template <typename Value> void check_words(const Value &actual, const Words &expected) {
  for (unsigned i = 0; i < actual.words.size(); ++i)
    CHECK(actual.words[i] == expected[i]);
}

void check_value(const Words &input) {
  ports::value.words = input;
  eval();
  for (unsigned c = 0; c < cases.size(); ++c) {
    const auto [width, limit] = cases[c];
    const unsigned value = input[0] & ((1u << width) - 1);
    if (value <= limit)
      CHECK(increased[c] == (value == limit ? limit : value + 1));
    CHECK(decreased[c] == (value == 0 ? 0 : value - 1));
  }

  Words value65{input[0], input[1], input[2] & 1u, 0, 0};
  const Words limit65{0, 0, 1, 0, 0};
  const Words limit129{0, 0, 0, 0, 1};
  const Words max129{UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, 1};
  if (less_equal(value65, limit65))
    check_words(inc_65, value65 == limit65 ? limit65 : successor(value65));
  if (less_equal(input, limit129))
    check_words(inc_129, input == limit129 ? limit129 : successor(input));
  check_words(inc_full_129, input == max129 ? max129 : successor(input));
  check_words(dec_65, predecessor(value65));
  check_words(dec_129, predecessor(input));
}

int main() {
  return run_test([] {
    for (unsigned value = 0; value < 65536; ++value)
      check_value(Words{value, 0, 0, 0, 0});

    // Cross each limb in both directions, including custom and full-width limits.
    for (unsigned limbs = 1; limbs <= 4; ++limbs) {
      Words boundary{};
      boundary[limbs] = 1;
      check_value(predecessor(boundary));
      check_value(boundary);
      check_value(successor(boundary));
    }
    const Words maximum{UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, 1};
    check_value(predecessor(maximum));
    check_value(maximum);
  });
}
