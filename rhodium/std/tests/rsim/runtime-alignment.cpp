// Exhaustively checks runtime alignment, explicit bounds, and mismatched
// operand widths.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

bool aligned(int address, int power, int bound) {
  if (power > bound)
    return 0;
  return (address % (1 << power)) == 0;
}

int main() {
  return run_test([] {
    for (int address = 0; address < 256; address++) {
      for (int power = 0; power < 64; power++) {
        value = ((address)&low_mask(8));
        exponent = ((power)&low_mask(6));
        eval();
        CHECK(full == aligned(address, power, 8));
        CHECK(bounded == aligned(address, power, 6));
        CHECK(five_bit == aligned(address & 31, power, 5));
        CHECK(single_bit == aligned(address & 1, power, 1));
        CHECK(narrow_exponent == aligned(address, power & 1, 8));
        CHECK(unit == (power == 0));
      }
    }
  });
}
