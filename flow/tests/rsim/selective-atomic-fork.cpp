// Verifies payload-selected atomic transfer, changing stalled offers, and empty
// selection.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    ingress_in = {
        .pvalid = UINT64_C(1),
        .pbits = {.pdestinations = UINT64_C(5), .pdata = UINT64_C(90)}};
    egress_0_in = {.pready = UINT64_C(1)};
    egress_1_in = {.pready = UINT64_C(0)};
    egress_2_in = {.pready = UINT64_C(0)};
    eval();
    CHECK(!ingress_out.pready && !egress_0_out.pvalid && !egress_1_out.pvalid &&
          egress_2_out.pvalid);

    egress_2_in.pready = UINT64_C(1);
    eval();
    CHECK(ingress_out.pready && egress_0_out.pvalid && !egress_1_out.pvalid &&
          egress_2_out.pvalid && egress_0_out.pbits.pdata == UINT64_C(90) &&
          egress_2_out.pbits.pdata == UINT64_C(90));

    egress_0_in.pready = UINT64_C(0);
    egress_1_in.pready = UINT64_C(1);
    egress_2_in.pready = UINT64_C(0);
    ingress_in.pbits = {.pdestinations = UINT64_C(3), .pdata = UINT64_C(166)};
    eval();
    CHECK(!ingress_out.pready && egress_0_out.pvalid && !egress_1_out.pvalid &&
          !egress_2_out.pvalid && egress_0_out.pbits.pdata == UINT64_C(166));

    ingress_in.pbits.pdestinations = UINT64_C(0);
    eval();
    CHECK(ingress_out.pready && !egress_0_out.pvalid && !egress_1_out.pvalid &&
          !egress_2_out.pvalid);

    ingress_in.pvalid = UINT64_C(0);
    eval();
    CHECK(!egress_0_out.pvalid && !egress_1_out.pvalid && !egress_2_out.pvalid);
  });
}
