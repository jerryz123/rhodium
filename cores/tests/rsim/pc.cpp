// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <array>
#include <cstdint>
#include <random>

constexpr std::array<unsigned, 5> widths64{1, 32, 39, 45, 64};
constexpr std::array<unsigned, 4> widths32{1, 16, 31, 32};

std::uint64_t mask(unsigned width) {
  return width == 64 ? UINT64_MAX : (UINT64_C(1) << width) - 1;
}

std::uint64_t normalize(std::uint64_t value, unsigned width, unsigned xlen) {
  auto result = value & mask(width);
  if (result & (UINT64_C(1) << (width - 1))) result |= ~mask(width);
  return result & mask(xlen);
}

int main() {
  return run_test([] {
    std::array<std::uint64_t, 5> held64{}, hint64{};
    std::array<std::uint64_t, 4> held32{}, hint32{};
    reset = 1;
    capture = 0;
    tick_model();
    reset = 0;
    auto cycle = [&](std::uint64_t value, bool enable) {
      address = value;
      capture = enable;
      eval();
      for (unsigned i = 0; i < widths64.size(); ++i) {
        const auto expected = normalize(value, widths64[i], 64);
        CHECK(fits64[i] == (value == expected));
        if (enable) {
          hint64[i] = expected;
          if (value == expected) held64[i] = value;
        }
      }
      for (unsigned i = 0; i < widths32.size(); ++i) {
        const auto source = value & mask(32);
        const auto expected = normalize(source, widths32[i], 32);
        CHECK(fits32[i] == (source == expected));
        if (enable) {
          hint32[i] = expected;
          if (source == expected) held32[i] = source;
        }
      }
      tick_model();
      for (unsigned i = 0; i < widths64.size(); ++i) {
        CHECK(exact64[i] == held64[i]);
        CHECK(predicted64[i] == hint64[i]);
      }
      for (unsigned i = 0; i < widths32.size(); ++i) {
        CHECK(exact32[i] == held32[i]);
        CHECK(predicted32[i] == hint32[i]);
      }
    };
    cycle(0, false);
    for (auto width : widths64) {
      const auto boundary = UINT64_C(1) << (width - 1);
      for (auto value : {boundary - 1, boundary, mask(width), ~boundary, UINT64_MAX}) {
        cycle(value, true);
        cycle(value ^ UINT64_MAX, false);
        cycle(normalize(value, width, 64), true);
      }
    }
    for (unsigned bit = 0; bit < 64; ++bit) {
      cycle(UINT64_C(1) << bit, true);
      cycle(~(UINT64_C(1) << bit), true);
    }
    std::mt19937_64 random(0x7063);
    for (unsigned n = 0; n < 3000; ++n) cycle(random(), n % 3 != 0);
  });
}
