// Exercises standard-B and Zicond resource controls in the reusable RV32 ALU.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void check_result(std::uint8_t result_select, std::uint8_t subselect,
                  bool flag_a, bool flag_b, std::uint32_t left_value,
                  std::uint32_t right_value, std::uint32_t expected) {
  control = {};
  control.presult_uselect = result_select;
  switch (result_select) {
  case UINT64_C(0): {
    control.pshift_uadd_uamount = slice(subselect, 1, 0);
  }

  break;
  case UINT64_C(1): {
    control.pshift_uright = flag_a;
    control.pextract_ubit = flag_b;
  }

  break;
  case UINT64_C(2): {
    control.plogic_uselect = slice(subselect, 1, 0);
    control.pinvert_uright = flag_a;
    control.plogic_uright_uselect = flag_b ? UINT64_C(1) : UINT64_C(0);
  }

  break;
  case UINT64_C(4): {
    control.psubtract = UINT64_C(1);
    control.psigned_ucompare = flag_a;
    control.pmaximum = flag_b;
  }

  break;
  case UINT64_C(5): {
    control.pcount_uselect = slice(subselect, 1, 0);
  }

  break;
  case UINT64_C(6): {
    control.pshift_uright = flag_a;
  }

  break;
  case UINT64_C(7): {
    control.punary_uselect = subselect;
  }

  break;
  default: {
  }

  break;
  }
  left = left_value;
  right = right_value;
  eval();
  CHECK(result == expected);
}

void check_conditional(bool invert_right, std::uint32_t left_value,
                       std::uint32_t right_value, std::uint32_t expected) {
  control = {};
  control.presult_uselect = UINT64_C(2);
  control.plogic_uselect = UINT64_C(0);
  control.pinvert_uright = invert_right;
  control.plogic_uright_uselect = UINT64_C(2);
  left = left_value;
  right = right_value;
  eval();
  CHECK(result == expected);
}

int main() {
  return run_test([] {
    check_result(0, 3, 0, 0, UINT64_C(3), UINT64_C(5), UINT64_C(29));
    check_result(2, 0, 1, 0, UINT64_C(65280), UINT64_C(3855), UINT64_C(61440));
    check_result(2, 1, 1, 0, UINT64_C(240), UINT64_C(255),
                 UINT64_C(4294967280));
    check_result(2, 2, 1, 0, UINT64_C(85), UINT64_C(170), UINT64_C(4294967040));
    check_result(5, 0, 0, 0, UINT64_C(16), 0, UINT64_C(27));
    check_result(5, 1, 0, 0, UINT64_C(256), 0, UINT64_C(8));
    check_result(5, 2, 0, 0, UINT64_C(3855), 0, UINT64_C(8));
    check_result(4, 0, 1, 0, -UINT64_C(1), UINT64_C(1), -UINT64_C(1));
    check_result(4, 0, 0, 1, -UINT64_C(1), UINT64_C(1), -UINT64_C(1));
    check_result(7, 0, 0, 0, UINT64_C(16777728), 0, UINT64_C(4278255360));
    check_result(7, 1, 0, 0, UINT64_C(19088743), 0, UINT64_C(1732584193));
    check_result(6, 0, 0, 0, UINT64_C(2147483649), 1, UINT64_C(3));
    check_result(6, 0, 1, 0, UINT64_C(2147483649), 1, UINT64_C(3221225472));
    check_result(7, 2, 0, 0, UINT64_C(128), 0, UINT64_C(4294967168));
    check_result(7, 3, 0, 0, UINT64_C(32769), 0, UINT64_C(4294934529));
    check_result(7, 4, 0, 0, UINT64_C(4294934529), 0, UINT64_C(32769));
    check_result(2, 0, 1, 1, UINT64_C(255), 3, UINT64_C(247));
    check_result(1, 0, 1, 1, UINT64_C(8), 3, UINT64_C(1));
    check_result(2, 2, 0, 1, UINT64_C(8), 3, UINT64_C(0));
    check_result(2, 1, 0, 1, UINT64_C(0), 31, UINT64_C(2147483648));
    check_conditional(0, UINT64_C(2309737967), 0, 0);
    check_conditional(0, UINT64_C(2309737967), 1, UINT64_C(2309737967));
    check_conditional(1, UINT64_C(2309737967), 0, UINT64_C(2309737967));
    check_conditional(1, UINT64_C(2309737967), 1, 0);
  });
}
