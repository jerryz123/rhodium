// Simulates enable, non-power-of-two rollover, wrap indication, and reset.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    enable = UINT64_C(0);
    tick_model();
    reset = UINT64_C(0);
    CHECK(value == UINT64_C(0) && !wrap);

    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    CHECK(value == UINT64_C(0));

    enable = UINT64_C(1);
    for (unsigned repeat_index = 0; repeat_index < (9); ++repeat_index)
      tick_model();
    CHECK(value == UINT64_C(9) && wrap);

    tick_model();
    CHECK(value == UINT64_C(0) && !wrap);

    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      tick_model();
    enable = UINT64_C(0);
    tick_model();
    CHECK(value == UINT64_C(3) && !wrap);

    reset = UINT64_C(1);
    tick_model();
    CHECK(value == UINT64_C(0));
  });
}
