// Exercises fixed priority, selected readiness, chosen index, and stalled
// preemption.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    ingress_0_in = {.pvalid = UINT64_C(0), .pbits = UINT64_C(160)};
    ingress_1_in = {.pvalid = UINT64_C(0), .pbits = UINT64_C(177)};
    ingress_2_in = {.pvalid = UINT64_C(0), .pbits = UINT64_C(194)};
    egress_in = {.pready = UINT64_C(0)};
    eval();
    CHECK(!egress_out.pvalid && chosen == UINT64_C(0) &&
          !ingress_0_out.pready && !ingress_1_out.pready &&
          !ingress_2_out.pready);

    ingress_2_in.pvalid = UINT64_C(1);
    eval();
    CHECK(egress_out.pvalid && egress_out.pbits == UINT64_C(194) &&
          chosen == UINT64_C(2));

    ingress_1_in.pvalid = UINT64_C(1);
    eval();
    CHECK(egress_out.pbits == UINT64_C(177) && chosen == UINT64_C(1));

    ingress_0_in.pvalid = UINT64_C(1);
    egress_in.pready = UINT64_C(1);
    eval();
    CHECK(egress_out.pbits == UINT64_C(160) && chosen == UINT64_C(0) &&
          ingress_0_out.pready && !ingress_1_out.pready &&
          !ingress_2_out.pready);

    ingress_0_in.pvalid = UINT64_C(0);
    eval();
    CHECK(egress_out.pbits == UINT64_C(177) && chosen == UINT64_C(1) &&
          ingress_1_out.pready && !ingress_2_out.pready);

    egress_in.pready = UINT64_C(0);
    ingress_0_in.pvalid = UINT64_C(1);
    eval();
    CHECK(egress_out.pbits == UINT64_C(160) && chosen == UINT64_C(0) &&
          !ingress_0_out.pready);
  });
}
