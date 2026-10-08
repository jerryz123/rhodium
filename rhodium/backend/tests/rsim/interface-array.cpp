// Checks interface-array behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    ingress_0_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(53)};
    ingress_1_in = {.pvalid = UINT64_C(0), .pbits = UINT64_C(202)};
    egress_0_in = {.pready = UINT64_C(0)};
    egress_1_in = {.pready = UINT64_C(1)};
    eval();

    if ((egress_0_out.pvalid != ingress_0_in.pvalid ||
         egress_0_out.pbits != ingress_0_in.pbits) ||
        (egress_1_out.pvalid != ingress_1_in.pvalid ||
         egress_1_out.pbits != ingress_1_in.pbits) ||
        ingress_0_out.pready != egress_0_in.pready ||
        ingress_1_out.pready != egress_1_in.pready)
      fail(1, "interface array lanes did not pass through independently");
  });
}
