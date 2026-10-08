// Exhaustively checks transfer containment, zero lengths, and address-space-end
// arithmetic.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    for (int a = 0; a < 16; a++) {
      for (int n = 0; n < 16; n++) {
        for (int b = 0; b < 16; b++) {
          address = ((a)&low_mask(4));
          length = ((n)&low_mask(4));
          base = ((b)&low_mask(4));
          eval();
          for (int size = 1; size <= 16; size++) {
            CHECK(contained[size - 1] ==
                  ((n > 0) && (a >= b) && (a + n <= b + size) &&
                   (b + size <= 16)));
          }
        }
      }
    }
  });
}
