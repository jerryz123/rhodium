// Checks reset-shift-register behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    reset = UINT64_C(1);
    data_in = UINT64_C(0);
    shift = UINT64_C(0);
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    CHECK(data_out == UINT64_C(0));

    reset = UINT64_C(0);
    shift = UINT64_C(1);
    data_in = UINT64_C(1);
    tick_model();
    data_in = UINT64_C(2);
    tick_model();
    data_in = UINT64_C(3);
    tick_model();
    data_in = UINT64_C(4);
    tick_model();
    CHECK(data_out == UINT64_C(1));

    shift = UINT64_C(0);
    data_in = UINT64_C(9);
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    CHECK(data_out == UINT64_C(1));

    reset = UINT64_C(1);
    tick_model();
    CHECK(data_out == UINT64_C(0));
  });
}
