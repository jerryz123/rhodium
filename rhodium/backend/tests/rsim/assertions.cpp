// Checks assertions behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    reset = UINT64_C(1);
    always_condition = UINT64_C(0);
    request = UINT64_C(0);
    request_condition = UINT64_C(0);
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();

    always_condition = UINT64_C(1);
    reset = UINT64_C(0);
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();

    request_condition = UINT64_C(1);
    request = UINT64_C(1);
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    request_condition = 0;
    expect_failure("request_holds", [] { tick_model(); });
    request = 0;
    tick_model();
    always_condition = 0;
    expect_failure("always_holds", [] { tick_model(); });
    reset = 1;
    request = 1;
    tick_model(); // Reset suppresses both otherwise failing assertions.
  });
}
