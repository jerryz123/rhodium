// Checks nested-circuit behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    value_in = UINT64_C(0);
    eval();
    CHECK(value_out == UINT64_C(1));

    value_in = UINT64_C(127);
    eval();
    CHECK(value_out == UINT64_C(128));

    value_in = UINT64_C(255);
    eval();
    CHECK(value_out == UINT64_C(0));
  });
}
