// Checks clocking-sync-level behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    reset = UINT64_C(0);
    asynchronous_level = UINT64_C(0);
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    CHECK(synchronized_level == UINT64_C(0));

    eval();
    asynchronous_level = UINT64_C(1);
    tick_model();
    CHECK(synchronized_level == UINT64_C(0));

    eval();
    reset = UINT64_C(1);
    tick_model();
    CHECK(synchronized_level == UINT64_C(1));

    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    CHECK(synchronized_level == UINT64_C(1));

    eval();
    asynchronous_level = UINT64_C(0);
    tick_model();
    CHECK(synchronized_level == UINT64_C(1));
    tick_model();
    CHECK(synchronized_level == UINT64_C(0));

    eval();
    reset = UINT64_C(0);
    asynchronous_level = UINT64_C(1);
    tick_model();
    CHECK(synchronized_level == UINT64_C(0));
    tick_model();
    CHECK(synchronized_level == UINT64_C(1));
  });
}
