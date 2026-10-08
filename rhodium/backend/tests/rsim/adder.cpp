// Checks adder behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    a = UINT64_C(1);
    b = UINT64_C(2);
    eval();
    CHECK(sum == UINT64_C(3));

    a = UINT64_C(255);
    b = UINT64_C(1);
    eval();
    CHECK(sum == UINT64_C(0));

    a = UINT64_C(87);
    b = UINT64_C(44);
    eval();
    CHECK(sum == UINT64_C(131));
  });
}
