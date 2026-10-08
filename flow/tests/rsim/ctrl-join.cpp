// Verifies that a token join never partially consumes its inputs.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    ingress_0_in = {.pvalid = UINT64_C(1)};
    ingress_1_in = {.pvalid = UINT64_C(1)};
    ingress_2_in = {.pvalid = UINT64_C(0)};
    egress_in = {.pready = UINT64_C(1)};
    eval();
    CHECK(!egress_out.pvalid && !ingress_0_out.pready &&
          !ingress_1_out.pready && ingress_2_out.pready);

    ingress_2_in.pvalid = UINT64_C(1);
    egress_in.pready = UINT64_C(0);
    eval();
    CHECK(egress_out.pvalid && !ingress_0_out.pready && !ingress_1_out.pready &&
          !ingress_2_out.pready);
    egress_in.pready = UINT64_C(1);
    eval();
    CHECK(ingress_0_out.pready && ingress_1_out.pready && ingress_2_out.pready);
  });
}
