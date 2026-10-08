// Simulates decoded RV64I controls through the ALU, including unconstrained
// modifiers.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void check_alu(std::uint32_t instruction_in, std::uint64_t left_in,
               std::uint64_t right_in, std::uint64_t expected) {
  instruction = instruction_in;
  left = left_in;
  right = right_in;
  eval();
  CHECK(valid == UINT64_C(1));
  CHECK(result == expected);
}

int main() {
  return run_test([] {
    check_alu(UINT64_C(19), UINT64_C(41), UINT64_C(1), UINT64_C(42));
    check_alu(UINT64_C(1073741875), UINT64_C(0), UINT64_C(1),
              UINT64_C(18446744073709551615));
    check_alu(UINT64_C(4115), UINT64_C(1), UINT64_C(7), UINT64_C(128));
    check_alu(UINT64_C(8243), UINT64_C(18446744073709551615), UINT64_C(0),
              UINT64_C(1));
    check_alu(UINT64_C(12339), UINT64_C(0), UINT64_C(18446744073709551615),
              UINT64_C(1));
    check_alu(UINT64_C(16403), UINT64_C(43605), UINT64_C(3855),
              UINT64_C(42330));
    check_alu(UINT64_C(1073762323), UINT64_C(9223372036854775808), UINT64_C(1),
              UINT64_C(13835058055282163712));
    check_alu(UINT64_C(24595), UINT64_C(61440), UINT64_C(3855),
              UINT64_C(65295));
    check_alu(UINT64_C(28691), UINT64_C(61680), UINT64_C(4080), UINT64_C(240));
    check_alu(UINT64_C(27), UINT64_C(2147483647), UINT64_C(1),
              UINT64_C(18446744071562067968));
    check_alu(UINT64_C(1073762331), UINT64_C(2147483648), UINT64_C(1),
              UINT64_C(18446744072635809792));

    instruction = UINT64_C(4294967295);
    left = {};
    right = {};
    eval();
    CHECK(valid == UINT64_C(0));
  });
}
