// Simulates every RV64I integer ALU resource-control combination and width edge
// case.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void check_adder(bool subtract, bool word, std::uint64_t left_value,
                 std::uint64_t right_value, std::uint64_t expected) {
  control = {};
  control.presult_uselect = UINT64_C(0);
  control.psubtract = subtract;
  control.pword = word;
  left = left_value;
  right = right_value;
  eval();
  CHECK(result == expected);
}

void check_shift(bool shift_right, bool arithmetic_shift, bool word,
                 std::uint64_t left_value, std::uint64_t right_value,
                 std::uint64_t expected) {
  control = {};
  control.presult_uselect = UINT64_C(1);
  control.pshift_uright = shift_right;
  control.parithmetic_ushift = arithmetic_shift;
  control.pword = word;
  left = left_value;
  right = right_value;
  eval();
  CHECK(result == expected);
}

void check_logic(std::uint8_t select, std::uint64_t left_value,
                 std::uint64_t right_value, std::uint64_t expected) {
  control = {};
  control.presult_uselect = UINT64_C(2);
  control.plogic_uselect = select;
  left = left_value;
  right = right_value;
  eval();
  CHECK(result == expected);
}

void check_compare(bool signed_compare, std::uint64_t left_value,
                   std::uint64_t right_value, std::uint64_t expected) {
  control = {};
  control.presult_uselect = UINT64_C(3);
  control.psubtract = UINT64_C(1);
  control.psigned_ucompare = signed_compare;
  left = left_value;
  right = right_value;
  eval();
  CHECK(result == expected);
}

int main() {
  return run_test([] {
    check_adder(0, 0, UINT64_C(18446744073709551615), UINT64_C(1), UINT64_C(0));
    check_adder(1, 0, UINT64_C(0), UINT64_C(1), UINT64_C(18446744073709551615));
    check_shift(0, 0, 0, UINT64_C(1), UINT64_C(63),
                UINT64_C(9223372036854775808));
    check_shift(0, 0, 0, UINT64_C(1), UINT64_C(64), UINT64_C(1));
    check_compare(1, UINT64_C(18446744073709551615), UINT64_C(0), UINT64_C(1));
    check_compare(1, UINT64_C(9223372036854775807),
                  UINT64_C(9223372036854775808), UINT64_C(0));
    check_compare(0, UINT64_C(18446744073709551615), UINT64_C(0), UINT64_C(0));
    check_compare(0, UINT64_C(0), UINT64_C(18446744073709551615), UINT64_C(1));
    check_logic(2, UINT64_C(12273903644374837845),
                UINT64_C(18446462603027742720), UINT64_C(6172933522750876245));
    check_shift(1, 0, 0, UINT64_C(9223372036854775808), UINT64_C(63),
                UINT64_C(1));
    check_shift(1, 0, 0, UINT64_C(9223372036854775808), UINT64_C(64),
                UINT64_C(9223372036854775808));
    check_shift(1, 1, 0, UINT64_C(9223372036854775808), UINT64_C(63),
                UINT64_C(18446744073709551615));
    check_shift(1, 1, 0, UINT64_C(9223372036854775808), UINT64_C(1),
                UINT64_C(13835058055282163712));
    check_logic(1, UINT64_C(61440), UINT64_C(3855), UINT64_C(65295));
    check_logic(0, UINT64_C(61680), UINT64_C(4080), UINT64_C(240));
    check_adder(0, 1, UINT64_C(2147483647), UINT64_C(1),
                UINT64_C(18446744071562067968));
    check_adder(0, 1, UINT64_C(18446744073709551615), UINT64_C(1), UINT64_C(0));
    check_adder(1, 1, UINT64_C(0), UINT64_C(1), UINT64_C(18446744073709551615));
    check_shift(0, 0, 1, UINT64_C(1), UINT64_C(31),
                UINT64_C(18446744071562067968));
    check_shift(0, 0, 1, UINT64_C(1), UINT64_C(32), UINT64_C(1));
    check_shift(1, 0, 1, UINT64_C(2147483648), UINT64_C(31), UINT64_C(1));
    check_shift(1, 1, 1, UINT64_C(2147483648), UINT64_C(31),
                UINT64_C(18446744073709551615));
    check_shift(1, 1, 1, UINT64_C(2147483648), UINT64_C(1),
                UINT64_C(18446744072635809792));
  });
}
