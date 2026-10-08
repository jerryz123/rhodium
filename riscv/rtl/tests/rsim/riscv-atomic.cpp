// Verifies every reusable RISC-V AMO function for RV64 doubleword and word
// operands.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
constexpr std::uint8_t WORD = UINT64_C(2);
constexpr std::uint8_t DOUBLE = UINT64_C(3);
constexpr std::uint8_t SWAP = UINT64_C(0);
constexpr std::uint8_t ADD = UINT64_C(1);
constexpr std::uint8_t XOR = UINT64_C(2);
constexpr std::uint8_t AND = UINT64_C(3);
constexpr std::uint8_t OR = UINT64_C(4);
constexpr std::uint8_t MIN = UINT64_C(5);
constexpr std::uint8_t MAX = UINT64_C(6);
constexpr std::uint8_t MINU = UINT64_C(7);
constexpr std::uint8_t MAXU = UINT64_C(8);

void check_atomic(std::uint8_t selected_operation, std::uint8_t selected_width,
                  std::uint64_t selected_old, std::uint64_t selected_operand,
                  std::uint64_t expected) {
  {
    operation = selected_operation;
    word = selected_width == WORD;
    old_value = selected_old;
    operand = selected_operand;
    eval();
    CHECK(value == expected);
  }
}

int main() {
  return run_test([] {
    check_atomic(SWAP, DOUBLE, UINT64_C(240), UINT64_C(15), UINT64_C(15));
    check_atomic(ADD, DOUBLE, UINT64_C(240), UINT64_C(15), UINT64_C(255));
    check_atomic(XOR, DOUBLE, UINT64_C(240), UINT64_C(15), UINT64_C(255));
    check_atomic(AND, DOUBLE, UINT64_C(240), UINT64_C(15), UINT64_C(0));
    check_atomic(OR, DOUBLE, UINT64_C(240), UINT64_C(15), UINT64_C(255));
    check_atomic(MIN, DOUBLE, UINT64_C(18446744073709551614), UINT64_C(3),
                 UINT64_C(18446744073709551614));
    check_atomic(MAX, DOUBLE, UINT64_C(18446744073709551614), UINT64_C(3),
                 UINT64_C(3));
    check_atomic(MINU, DOUBLE, UINT64_C(18446744073709551614), UINT64_C(3),
                 UINT64_C(3));
    check_atomic(MAXU, DOUBLE, UINT64_C(18446744073709551614), UINT64_C(3),
                 UINT64_C(18446744073709551614));
    check_atomic(ADD, WORD, UINT64_C(4294967295), UINT64_C(2), UINT64_C(1));
    check_atomic(MIN, WORD, UINT64_C(4294967294), UINT64_C(3),
                 UINT64_C(4294967294));
    check_atomic(MAXU, WORD, UINT64_C(4294967294), UINT64_C(3),
                 UINT64_C(4294967294));
  });
}
