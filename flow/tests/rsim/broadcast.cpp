// Verifies exactly-once broadcast delivery under independent output stalls.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    ingress_in = {.pvalid = UINT64_C(0), .pbits = UINT64_C(0)};
    egress_0_in = {.pready = UINT64_C(0)};
    egress_1_in = {.pready = UINT64_C(0)};
    egress_2_in = {.pready = UINT64_C(0)};
    tick_model();
    reset = UINT64_C(0);

    ingress_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(165)};
    CHECK(ingress_out.pready);
    tick_model();
    ingress_in.pvalid = UINT64_C(0);
    CHECK(egress_0_out.pvalid && egress_1_out.pvalid && egress_2_out.pvalid &&
          egress_0_out.pbits == UINT64_C(165) && !ingress_out.pready);

    egress_0_in.pready = UINT64_C(1);
    tick_model();
    CHECK(!egress_0_out.pvalid && egress_1_out.pvalid && egress_2_out.pvalid &&
          egress_1_out.pbits == UINT64_C(165));

    egress_2_in.pready = UINT64_C(1);
    tick_model();
    CHECK(!egress_0_out.pvalid && egress_1_out.pvalid && !egress_2_out.pvalid &&
          egress_1_out.pbits == UINT64_C(165));

    ingress_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(60)};
    egress_1_in.pready = UINT64_C(1);
    eval();
    CHECK(ingress_out.pready);
    tick_model();
    ingress_in.pvalid = UINT64_C(0);
    CHECK(egress_0_out.pvalid && egress_1_out.pvalid && egress_2_out.pvalid &&
          egress_0_out.pbits == UINT64_C(60) &&
          egress_1_out.pbits == UINT64_C(60) &&
          egress_2_out.pbits == UINT64_C(60));
  });
}
