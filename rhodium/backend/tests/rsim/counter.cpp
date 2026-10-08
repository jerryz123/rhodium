// Simulates enable, variable increments, modular wrap, and synchronous reset.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    reset = UINT64_C(1);
    inc = UINT64_C(0);
    amt = UINT64_C(3);
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    CHECK(tot == UINT64_C(0));

    reset = UINT64_C(0);
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    CHECK(tot == UINT64_C(0));

    inc = UINT64_C(1);
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      tick_model();
    CHECK(tot == UINT64_C(9));

    amt = UINT64_C(15);
    for (unsigned repeat_index = 0; repeat_index < (16); ++repeat_index)
      tick_model();
    CHECK(tot == UINT64_C(249));

    amt = UINT64_C(7);
    tick_model();
    CHECK(tot == UINT64_C(0));

    reset = UINT64_C(1);
    tick_model();
    CHECK(tot == UINT64_C(0));
  });
}
