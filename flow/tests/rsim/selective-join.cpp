// Verifies selective joining consumes the control and exactly the selected data
// lanes atomically.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    selection_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(5)};
    ingress_0_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(160)};
    ingress_1_in = {.pvalid = UINT64_C(0), .pbits = UINT64_C(177)};
    ingress_2_in = {.pvalid = UINT64_C(0), .pbits = UINT64_C(194)};
    egress_in = {.pready = UINT64_C(1)};
    eval();
    CHECK(!egress_out.pvalid && !selection_out.pready &&
          !ingress_0_out.pready && !ingress_1_out.pready &&
          ingress_2_out.pready);

    ingress_2_in.pvalid = UINT64_C(1);
    egress_in.pready = UINT64_C(0);
    eval();
    CHECK(egress_out.pvalid && !selection_out.pready && !ingress_0_out.pready &&
          !ingress_1_out.pready && !ingress_2_out.pready &&
          egress_out.pbits.pselection == UINT64_C(5) &&
          egress_out.pbits.pvalues[0] == UINT64_C(160) &&
          egress_out.pbits.pvalues[2] == UINT64_C(194));

    egress_in.pready = UINT64_C(1);
    eval();
    CHECK(selection_out.pready && ingress_0_out.pready &&
          !ingress_1_out.pready && ingress_2_out.pready);

    selection_in.pbits = UINT64_C(2);
    ingress_0_in.pvalid = UINT64_C(0);
    ingress_1_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(211)};
    ingress_2_in.pvalid = UINT64_C(0);
    egress_in.pready = UINT64_C(0);
    eval();
    CHECK(egress_out.pvalid && egress_out.pbits.pselection == UINT64_C(2) &&
          egress_out.pbits.pvalues[1] == UINT64_C(211) &&
          !selection_out.pready);

    selection_in.pbits = UINT64_C(0);
    egress_in.pready = UINT64_C(1);
    eval();
    CHECK(egress_out.pvalid && selection_out.pready && !ingress_0_out.pready &&
          !ingress_1_out.pready && !ingress_2_out.pready &&
          egress_out.pbits.pselection == UINT64_C(0));

    selection_in.pvalid = UINT64_C(0);
    eval();
    CHECK(!egress_out.pvalid && !ingress_0_out.pready &&
          !ingress_1_out.pready && !ingress_2_out.pready);
  });
}
