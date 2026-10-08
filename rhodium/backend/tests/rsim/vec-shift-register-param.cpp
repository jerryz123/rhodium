// Checks vec-shift-register-param behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    data_in = UINT64_C(0);
    tick_model();
    CHECK(data_out == UINT64_C(0));

    reset = UINT64_C(0);
    data_in = UINT64_C(1);
    tick_model();
    CHECK(data_out == UINT64_C(0));
    data_in = UINT64_C(2);
    tick_model();
    CHECK(data_out == UINT64_C(0));
    data_in = UINT64_C(3);
    tick_model();
    CHECK(data_out == UINT64_C(0));
    data_in = UINT64_C(4);
    tick_model();
    CHECK(data_out == UINT64_C(1));
    data_in = UINT64_C(5);
    tick_model();
    CHECK(data_out == UINT64_C(2));

    reset = UINT64_C(1);
    tick_model();
    CHECK(data_out == UINT64_C(0));
  });
}
