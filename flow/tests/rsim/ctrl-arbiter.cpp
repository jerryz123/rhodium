// Exercises fixed priority, chosen index, and selected token readiness.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    ingress_0_in = {.pvalid = UINT64_C(0)};
    ingress_1_in = {.pvalid = UINT64_C(1)};
    ingress_2_in = {.pvalid = UINT64_C(1)};
    egress_in = {.pready = UINT64_C(1)};
    eval();
    CHECK(egress_out.pvalid && chosen == UINT64_C(1) && !ingress_0_out.pready &&
          ingress_1_out.pready && !ingress_2_out.pready);

    ingress_0_in.pvalid = UINT64_C(1);
    eval();
    CHECK(chosen == UINT64_C(0) && ingress_0_out.pready &&
          !ingress_1_out.pready && !ingress_2_out.pready);

    egress_in.pready = UINT64_C(0);
    eval();
    CHECK(egress_out.pvalid && !ingress_0_out.pready);
  });
}
