// Exercises every standard-B and Zicond resource family in the RV64 ALU.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void apply_and_check(std::uint64_t left_value, std::uint64_t right_value,
                     std::uint64_t expected) {
  left = left_value;
  right = right_value;
  eval();
  CHECK(result == expected);
}

void check_adder(bool unsigned_word, std::uint8_t shift_amount,
                 std::uint64_t left_value, std::uint64_t right_value,
                 std::uint64_t expected) {
  control = {};
  control.presult_uselect = UINT64_C(0);
  control.punsigned_uword = unsigned_word;
  control.pshift_uadd_uamount = shift_amount;
  apply_and_check(left_value, right_value, expected);
}

void check_shift(bool shift_right, bool extract_bit, bool unsigned_word,
                 std::uint64_t left_value, std::uint64_t right_value,
                 std::uint64_t expected) {
  control = {};
  control.presult_uselect = UINT64_C(1);
  control.pshift_uright = shift_right;
  control.pextract_ubit = extract_bit;
  control.punsigned_uword = unsigned_word;
  apply_and_check(left_value, right_value, expected);
}

void check_logic(std::uint8_t select, bool invert_right, bool one_hot_right,
                 std::uint64_t left_value, std::uint64_t right_value,
                 std::uint64_t expected) {
  control = {};
  control.presult_uselect = UINT64_C(2);
  control.plogic_uselect = select;
  control.pinvert_uright = invert_right;
  control.plogic_uright_uselect = one_hot_right ? UINT64_C(1) : UINT64_C(0);
  apply_and_check(left_value, right_value, expected);
}

void check_conditional(bool invert_right, std::uint64_t left_value,
                       std::uint64_t right_value, std::uint64_t expected) {
  control = {};
  control.presult_uselect = UINT64_C(2);
  control.plogic_uselect = UINT64_C(0);
  control.pinvert_uright = invert_right;
  control.plogic_uright_uselect = UINT64_C(2);
  apply_and_check(left_value, right_value, expected);
}

void check_minmax(bool signed_compare, bool maximum, std::uint64_t left_value,
                  std::uint64_t right_value, std::uint64_t expected) {
  control = {};
  control.presult_uselect = UINT64_C(4);
  control.psubtract = UINT64_C(1);
  control.psigned_ucompare = signed_compare;
  control.pmaximum = maximum;
  apply_and_check(left_value, right_value, expected);
}

void check_count(std::uint8_t select, bool word, std::uint64_t left_value,
                 std::uint64_t expected) {
  control = {};
  control.presult_uselect = UINT64_C(5);
  control.pcount_uselect = select;
  control.pword = word;
  apply_and_check(left_value, 0, expected);
}

void check_rotate(bool shift_right, bool word, std::uint64_t left_value,
                  std::uint64_t right_value, std::uint64_t expected) {
  control = {};
  control.presult_uselect = UINT64_C(6);
  control.pshift_uright = shift_right;
  control.pword = word;
  apply_and_check(left_value, right_value, expected);
}

void check_unary(std::uint8_t select, std::uint64_t left_value,
                 std::uint64_t expected) {
  control = {};
  control.presult_uselect = UINT64_C(7);
  control.punary_uselect = select;
  apply_and_check(left_value, 0, expected);
}

int main() {
  return run_test([] {
    check_adder(0, 2, UINT64_C(3), UINT64_C(5), UINT64_C(17));
    check_adder(1, 1, UINT64_C(18446744071562067969), UINT64_C(2),
                UINT64_C(4294967300));
    check_shift(0, 0, 1, UINT64_C(18446744071562067969), UINT64_C(4),
                UINT64_C(34359738384));
    check_logic(0, 1, 0, UINT64_C(65280), UINT64_C(3855), UINT64_C(61440));
    check_logic(1, 1, 0, UINT64_C(240), UINT64_C(255),
                UINT64_C(18446744073709551600));
    check_logic(2, 1, 0, UINT64_C(85), UINT64_C(170),
                UINT64_C(18446744073709551360));
    check_count(0, 0, UINT64_C(16), UINT64_C(59));
    check_count(1, 0, UINT64_C(256), UINT64_C(8));
    check_count(2, 0, UINT64_C(3855), UINT64_C(8));
    check_count(0, 1, UINT64_C(18446744069414584336), UINT64_C(27));
    check_count(1, 1, UINT64_C(18446744069414584320), UINT64_C(32));
    check_count(2, 1, UINT64_C(18446744069414584335), UINT64_C(4));
    check_minmax(1, 0, -UINT64_C(1), UINT64_C(1), -UINT64_C(1));
    check_minmax(0, 1, -UINT64_C(1), UINT64_C(1), -UINT64_C(1));
    check_unary(0, UINT64_C(72057602627863296), UINT64_C(18374687574888349440));
    check_unary(1, UINT64_C(81985529216486895), UINT64_C(17279655951921914625));
    check_rotate(0, 0, UINT64_C(9223372036854775809), 1, UINT64_C(3));
    check_rotate(1, 1, UINT64_C(2147483649), 1, UINT64_C(18446744072635809792));
    check_unary(2, UINT64_C(128), UINT64_C(18446744073709551488));
    check_unary(3, UINT64_C(32769), UINT64_C(18446744073709518849));
    check_unary(4, UINT64_C(18446744073709518849), UINT64_C(32769));
    check_logic(0, 1, 1, UINT64_C(255), 3, UINT64_C(247));
    check_shift(1, 1, 0, UINT64_C(8), 3, UINT64_C(1));
    check_logic(2, 0, 1, UINT64_C(8), 3, UINT64_C(0));
    check_logic(1, 0, 1, UINT64_C(0), 63, UINT64_C(9223372036854775808));
    check_conditional(0, UINT64_C(81985529216486895), 0, 0);
    check_conditional(0, UINT64_C(81985529216486895), 1,
                      UINT64_C(81985529216486895));
    check_conditional(1, UINT64_C(81985529216486895), 0,
                      UINT64_C(81985529216486895));
    check_conditional(1, UINT64_C(81985529216486895), 1, 0);
  });
}
