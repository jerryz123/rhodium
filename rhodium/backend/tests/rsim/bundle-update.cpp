// Checks immutable replacement, register/memory history, and propagated methods.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
using Bundle = std::remove_cvref_t<decltype(source)>;
unsigned packed(const Bundle &b) {
  return (b.ppayload.pdata << 6) | (b.ppayload.ptag << 2) |
         (b.ppayload.pstate << 1) | b.pvalid;
}
int main() {
  return run_test([] {
    reset = 1;
    tick_model();
    reset = 0;
    tick_model();
    unsigned previous = 0;
    for (unsigned i = 0; i < 256; ++i) {
      const unsigned value = ((i * 73) ^ (i << 5)) & 16383;
      source = {{std::uint8_t(value >> 6), std::uint8_t(value >> 2 & 15),
                 std::uint8_t(value >> 1 & 1)},
                std::uint8_t(value & 1)};
      data = 255 - i;
      tag = i & 15;
      choose = i & 1;
      eval();
      CHECK(packed(original) == value);
      CHECK(packed(changed) == ((data << 6) | (tag << 2) | (value & 3)));
      CHECK(((swapped.pdata << 4) | swapped.ptag) == (value >> 2));
      CHECK(((plain.pdata << 1) | plain.pstate) ==
            ((data << 1) | (value >> 1 & 1)));
      CHECK(((tagged_payload.pdata << 5) | (tagged_payload.ptag << 1) |
             tagged_payload.pstate) ==
            ((data << 5) | (tag << 1) | (value >> 1 & 1)));
      unsigned saved = (previous & ~1u) | (value & 1);
      CHECK(packed(registered) == saved && packed(inferred) == saved &&
            packed(remembered) == saved);
      CHECK(packed(observed) == (value | 1));
      unsigned current_bit = value >> 1 & 1, old_bit = previous >> 1 & 1;
      unsigned expected = (current_bit << 8) | (current_bit << 7) |
                          (old_bit << 6) | (current_bit << 5) |
                          (current_bit << 4) | ((choose & current_bit) << 3) |
                          (old_bit << 2) | (current_bit << 1) | current_bit;
      CHECK(pack_bits(methods) == expected);
      tick_model();
      previous = value;
    }
  });
}
