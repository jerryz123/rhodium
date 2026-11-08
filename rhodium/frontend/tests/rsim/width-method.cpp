// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    for (unsigned v = 0; v < 16; ++v) {
      for (unsigned b = 0; b < 2; ++b) {
        value = v;
        signed_value = v;
        bit = b;
        eval();
        const auto extended = std::uint64_t(v < 8 ? int(v) : int(v) - 16);
        CHECK(sign == (extended & 4095));
        CHECK(signed_result == (extended & 4095));
        CHECK(zero == v && same == v && ssame == v);
        CHECK(tiny == (b ? 4095 : 0));
        CHECK(wide_low == extended);
        CHECK(wide_mid == (v < 8 ? 0 : UINT64_MAX));
        CHECK(wide_top == (v >= 8));
      }
    }
  });
}
