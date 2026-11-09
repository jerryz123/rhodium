// Simulates RV32I integer ALU resource controls and shift-width edge cases.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void check_alu(std::uint8_t result_select, bool subtract, bool signed_compare,
               bool shift_right, bool arithmetic_shift,
               std::uint8_t logic_select, std::uint32_t left_value,
               std::uint32_t right_value, std::uint32_t expected) {
  control = {};
  control.presult_uselect = result_select;
  control.psubtract = subtract;
  control.psigned_ucompare = signed_compare;
  control.pshift_uright = shift_right;
  control.parithmetic_ushift = arithmetic_shift;
  control.plogic_uselect = logic_select;
  left = left_value;
  right = right_value;
  eval();
  CHECK(result == expected);
}

int main() {
  return run_test([] {
    check_alu(0, 0, 0, 0, 0, 0, UINT64_C(4294967295), UINT64_C(1), UINT64_C(0));
    check_alu(0, 1, 0, 0, 0, 0, UINT64_C(0), UINT64_C(1), UINT64_C(4294967295));
    check_alu(1, 0, 0, 0, 0, 0, UINT64_C(1), UINT64_C(31),
              UINT64_C(2147483648));
    check_alu(1, 0, 0, 0, 0, 0, UINT64_C(1), UINT64_C(32), UINT64_C(1));
    check_alu(3, 1, 1, 0, 0, 0, UINT64_C(4294967295), UINT64_C(0), UINT64_C(1));
    check_alu(3, 1, 1, 0, 0, 0, UINT64_C(2147483647), UINT64_C(2147483648),
              UINT64_C(0));
    check_alu(3, 1, 0, 0, 0, 0, UINT64_C(4294967295), UINT64_C(0), UINT64_C(0));
    check_alu(3, 1, 0, 0, 0, 0, UINT64_C(0), UINT64_C(4294967295), UINT64_C(1));
    check_alu(2, 0, 0, 0, 0, 2, UINT64_C(2857740885), UINT64_C(4294901760),
              UINT64_C(1437248085));
    check_alu(2, 0, 0, 0, 0, 1, UINT64_C(61440), UINT64_C(3855),
              UINT64_C(65295));
    check_alu(2, 0, 0, 0, 0, 0, UINT64_C(61680), UINT64_C(4080), UINT64_C(240));
    check_alu(1, 0, 0, 1, 0, 0, UINT64_C(2147483648), UINT64_C(31),
              UINT64_C(1));
    check_alu(1, 0, 0, 1, 0, 0, UINT64_C(2147483648), UINT64_C(32),
              UINT64_C(2147483648));
    check_alu(1, 0, 0, 1, 1, 0, UINT64_C(2147483648), UINT64_C(31),
              UINT64_C(4294967295));
    check_alu(1, 0, 0, 1, 1, 0, UINT64_C(2147483648), UINT64_C(1),
              UINT64_C(3221225472));
  });
}
