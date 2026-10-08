// Checks enum-state behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    tick_model();
    CHECK(state_out == UINT64_C(0));

    reset = UINT64_C(0);
    tick_model();
    CHECK(state_out == UINT64_C(1));
    tick_model();
    CHECK(state_out == UINT64_C(2));
    tick_model();
    CHECK(state_out == UINT64_C(0));
  });
}
