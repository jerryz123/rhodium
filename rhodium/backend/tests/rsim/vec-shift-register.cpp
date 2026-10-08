// Checks vec-shift-register behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = UINT64_C(0);
    ins[0] = UINT64_C(1);
    ins[1] = UINT64_C(2);
    ins[2] = UINT64_C(3);
    ins[3] = UINT64_C(4);
    load = UINT64_C(1);
    shift = UINT64_C(0);
    tick_model();
    CHECK(out == UINT64_C(4));

    load = UINT64_C(0);
    shift = UINT64_C(1);
    ins[0] = UINT64_C(9);
    tick_model();
    CHECK(out == UINT64_C(3));
    tick_model();
    CHECK(out == UINT64_C(2));
    tick_model();
    CHECK(out == UINT64_C(1));
    tick_model();
    CHECK(out == UINT64_C(9));

    ins[0] = UINT64_C(5);
    ins[1] = UINT64_C(6);
    ins[2] = UINT64_C(7);
    ins[3] = UINT64_C(8);
    load = UINT64_C(1);
    shift = UINT64_C(1);
    tick_model();
    CHECK(out == UINT64_C(8));

    load = UINT64_C(0);
    shift = UINT64_C(0);
    ins[3] = UINT64_C(15);
    tick_model();
    CHECK(out == UINT64_C(8));
  });
}
