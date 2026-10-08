// Verifies that a join never partially consumes its ready-valid inputs.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    ingress_0_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(160)};
    ingress_1_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(177)};
    ingress_2_in = {.pvalid = UINT64_C(0), .pbits = UINT64_C(194)};
    egress_in = {.pready = UINT64_C(1)};
    eval();
    CHECK(!egress_out.pvalid && !ingress_0_out.pready &&
          !ingress_1_out.pready && ingress_2_out.pready);

    ingress_2_in.pvalid = UINT64_C(1);
    egress_in.pready = UINT64_C(0);
    eval();
    CHECK(egress_out.pvalid && !ingress_0_out.pready && !ingress_1_out.pready &&
          !ingress_2_out.pready && egress_out.pbits[0] == UINT64_C(160) &&
          egress_out.pbits[1] == UINT64_C(177) &&
          egress_out.pbits[2] == UINT64_C(194));

    egress_in.pready = UINT64_C(1);
    eval();
    CHECK(ingress_0_out.pready && ingress_1_out.pready && ingress_2_out.pready);
  });
}
