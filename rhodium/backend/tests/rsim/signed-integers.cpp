// Checks signed arithmetic, comparisons, overflow, and resizing through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
void check(int x, int y, unsigned shift, unsigned wide_shift) {
  a = std::uint8_t(x);
  b = std::uint8_t(y);
  amount = shift;
  wide_amount = wide_shift;
  eval();
  CHECK(sum == std::uint8_t(x + y));
  CHECK(difference == std::uint8_t(x - y));
  CHECK(product == std::uint8_t(x * y));
  CHECK(left == ((std::uint64_t(std::uint8_t(x)) << shift) & 255));
  CHECK(right == std::uint8_t(x >> shift));
  CHECK(right_wide == std::uint8_t(x >> wide_shift));
  CHECK(widened == (std::uint64_t(x) & 4095));
  CHECK(narrowed == (std::uint64_t(x) & 15));
  CHECK(lt == (x < y));
  CHECK(gt == (x > y));
  CHECK(le == (x <= y));
  CHECK(ge == (x >= y));
  CHECK(eq == (x == y));
  CHECK(negative_one == 255);
}
int main() {
  return run_test([] {
    check(-5, 3, 1, 1);
    check(-128, 127, 7, 12);
    check(100, 40, 2, 20);
  });
}
