// Verifies registered non-power-of-two scoreboard set, clear, and retention
// behavior.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    reset = UINT64_C(1);
    set_in = {};
    clear_in = {};
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    reset = UINT64_C(0);

    set_in.pvalid = UINT64_C(1);
    set_in.pbits = UINT64_C(2);
    eval();
    CHECK(busy == UINT64_C(0));
    tick_model();
    set_in.pvalid = UINT64_C(0);
    CHECK(busy == UINT64_C(4));

    clear_in.pvalid = UINT64_C(1);
    clear_in.pbits = UINT64_C(2);
    eval();
    CHECK(busy == UINT64_C(4));
    tick_model();
    clear_in.pvalid = UINT64_C(0);
    CHECK(busy == UINT64_C(0));

    set_in.pvalid = UINT64_C(1);
    set_in.pbits = UINT64_C(0);
    eval();
    CHECK(busy == UINT64_C(0));
    tick_model();
    set_in.pbits = UINT64_C(1);
    clear_in.pvalid = UINT64_C(1);
    clear_in.pbits = UINT64_C(1);
    CHECK(busy == UINT64_C(1));
    tick_model();
    set_in.pvalid = UINT64_C(0);
    clear_in.pbits = UINT64_C(0);
    eval();
    CHECK(busy == UINT64_C(1));
    tick_model();
    clear_in.pvalid = UINT64_C(0);
    CHECK(busy == UINT64_C(0));
  });
}
