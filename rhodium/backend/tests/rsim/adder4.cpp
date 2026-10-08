// Checks adder4 behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void check_add(std::uint8_t left, std::uint8_t right, bool carry_in,
               std::uint8_t expected) {
  {
    A = left;
    B = right;
    Cin = carry_in;
    eval();
    if (((Cout << 4) | Sum) != expected)
      fail(1, "adder mismatch: %0d + %0d + %0d", left, right, carry_in);
  }
}

int main() {
  return run_test([] {
    check_add(UINT64_C(3), UINT64_C(5), UINT64_C(0), UINT64_C(8));
    check_add(UINT64_C(15), UINT64_C(1), UINT64_C(0), UINT64_C(16));
    check_add(UINT64_C(9), UINT64_C(6), UINT64_C(1), UINT64_C(16));
    check_add(UINT64_C(7), UINT64_C(4), UINT64_C(1), UINT64_C(12));
  });
}
