// Exhaustively checks lane-mask expansion, single enables, slices, and non-byte
// lane widths.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    for (int pattern = 0; pattern < 256; pattern++) {
      for (int enabled = 0; enabled < 2; enabled++) {
        mask = ((pattern)&low_mask(8));
        single = ((enabled)&low_mask(1));
        eval();
        CHECK(lanes_1 == mask && single_1 == single);
        for (int bit_index = 0; bit_index < 24; bit_index++)
          CHECK(((lanes_3 >> (bit_index)) & 1) ==
                ((mask >> (bit_index / 3)) & 1));
        for (int bit_index = 0; bit_index < 64; bit_index++)
          CHECK(((lanes_8 >> (bit_index)) & 1) ==
                ((mask >> (bit_index / 8)) & 1));
        for (int bit_index = 0; bit_index < 7; bit_index++)
          CHECK(((single_7 >> (bit_index)) & 1) == single);
        for (int bit_index = 0; bit_index < 20; bit_index++)
          CHECK(((sliced_5 >> (bit_index)) & 1) ==
                ((mask >> (2 + bit_index / 5)) & 1));
      }
    }
  });
}
