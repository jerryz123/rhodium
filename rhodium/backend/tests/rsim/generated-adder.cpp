// Checks generated-adder behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void check_add(std::uint8_t left, std::uint8_t right, bool carry_in,
               std::uint16_t expected) {
  {
    A = left;
    B = right;
    Cin = carry_in;
    eval();
    CHECK(((Cout << 8) | Sum) == expected);
  }
}

int main() {
  return run_test([] {
    check_add(UINT64_C(3), UINT64_C(5), UINT64_C(0), UINT64_C(8));
    check_add(UINT64_C(255), UINT64_C(1), UINT64_C(0), UINT64_C(256));
    check_add(UINT64_C(128), UINT64_C(127), UINT64_C(1), UINT64_C(256));
    check_add(UINT64_C(85), UINT64_C(170), UINT64_C(0), UINT64_C(255));
  });
}
