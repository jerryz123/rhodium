// Verifies reusable RISC-V NaN, rounding, move, classification, and flag
// policy.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    value = UINT64_C(1065353216);
    other = UINT64_C(3221225472);
    container = UINT64_C(18446744070479937536);
    integer_value = UINT64_C(81985529216486895);
    instruction_mode = UINT64_C(7);
    dynamic_mode = UINT64_C(3);
    sign_operation = UINT64_C(0);
    invalid = UINT64_C(1);
    infinite = UINT64_C(1);
    overflow = UINT64_C(1);
    underflow = UINT64_C(1);
    inexact = UINT64_C(1);
    eval();
    CHECK(boxed == UINT64_C(18446744070479937536) && box_valid &&
          unboxed == UINT64_C(1065353216));
    CHECK(canonical_nan == UINT64_C(2143289344) && canonicalized == value);
    CHECK(rounding_valid && rounding_mode == UINT64_C(3));
    CHECK(flags == UINT64_C(31) && integer_flags == UINT64_C(17));
    CHECK(sign_result == UINT64_C(3212836864));
    CHECK(integer_move == UINT64_C(1065353216) &&
          float_move == UINT64_C(2309737967));
    CHECK(classification == UINT64_C(64));

    container = UINT64_C(1065353216);
    instruction_mode = UINT64_C(7);
    dynamic_mode = UINT64_C(5);
    value = UINT64_C(2141192193);
    other = UINT64_C(1073741824);
    eval();
    CHECK(!box_valid && unboxed == UINT64_C(2143289344));
    CHECK(canonicalized == UINT64_C(2143289344));
    CHECK(!rounding_valid);
    CHECK(classification == UINT64_C(256));
  });
}
