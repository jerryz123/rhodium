// Verifies that an atomic fork never permits a partial ready-valid transfer.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    ingress_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(90)};
    egress_0_in = {.pready = UINT64_C(1)};
    egress_1_in = {.pready = UINT64_C(0)};
    egress_2_in = {.pready = UINT64_C(1)};
    eval();
    CHECK(!ingress_out.pready && !(egress_0_out.pvalid && egress_0_in.pready) &&
          egress_1_out.pvalid && !(egress_2_out.pvalid && egress_2_in.pready));

    egress_1_in.pready = UINT64_C(1);
    eval();
    CHECK(ingress_out.pready && egress_0_out.pvalid && egress_1_out.pvalid &&
          egress_2_out.pvalid && egress_0_out.pbits == UINT64_C(90) &&
          egress_1_out.pbits == UINT64_C(90) &&
          egress_2_out.pbits == UINT64_C(90));

    ingress_in.pvalid = UINT64_C(0);
    eval();
    CHECK(!egress_0_out.pvalid && !egress_1_out.pvalid && !egress_2_out.pvalid);
  });
}
