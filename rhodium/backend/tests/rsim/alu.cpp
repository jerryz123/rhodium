// Checks alu behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void check_operation(std::uint8_t selected_op, std::uint8_t expected,
                     const char *label) {
  op = selected_op;
  eval();
  CHECK(result == expected);
}

int main() {
  return run_test([] {
    a = UINT64_C(204);
    b = UINT64_C(170);
    eval();
    CHECK(!equal);

    check_operation(UINT64_C(0), UINT64_C(136), "AND");
    check_operation(UINT64_C(1), UINT64_C(238), "OR");
    check_operation(UINT64_C(2), UINT64_C(102), "XOR");
    check_operation(UINT64_C(3), UINT64_C(118), "modular ADD");
    check_operation(UINT64_C(4), UINT64_C(34), "modular SUB");
    check_operation(UINT64_C(5), UINT64_C(51), "NOT-A");

    a = UINT64_C(90);
    b = UINT64_C(90);
    op = UINT64_C(3);
    eval();
    CHECK(equal);
    CHECK(result == UINT64_C(180));
  });
}
