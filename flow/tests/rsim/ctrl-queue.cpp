// Exercises token FIFO capacity, count, full backpressure, and draining.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    ingress_in = {.pvalid = UINT64_C(0)};
    egress_in = {.pready = UINT64_C(0)};
    tick_model();
    CHECK(!egress_out.pvalid && ingress_out.pready && count == UINT64_C(0));

    reset = UINT64_C(0);
    ingress_in.pvalid = UINT64_C(1);
    for (int i = 0; i < 3; ++i) {
      CHECK(ingress_out.pready);
      tick_model();
    }
    CHECK(egress_out.pvalid && !ingress_out.pready && count == UINT64_C(3));
    tick_model();
    CHECK(count == UINT64_C(3));

    ingress_in.pvalid = UINT64_C(0);
    egress_in.pready = UINT64_C(1);
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      tick_model();
    CHECK(!egress_out.pvalid && ingress_out.pready && count == UINT64_C(0));
  });
}
