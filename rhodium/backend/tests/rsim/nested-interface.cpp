// Checks nested-interface behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    ingress_in = {.pcommand = {.pvalid = UINT64_C(1), .pbits = UINT64_C(165)},
                  .penable = UINT64_C(1),
                  .presponse = {.pready = UINT64_C(1)}};
    egress_in = {.pcommand = {.pready = UINT64_C(1)},
                 .presponse = {.pvalid = UINT64_C(1), .pbits = UINT64_C(90)},
                 .pstatus = UINT64_C(60)};
    eval();

    if (egress_out.pcommand.pvalid != UINT64_C(1) ||
        egress_out.pcommand.pbits != UINT64_C(165) ||
        egress_out.penable != UINT64_C(1) ||
        egress_out.presponse.pready != UINT64_C(1))
      fail(1, "nested forward interface flow failed");
    if (ingress_out.pcommand.pready != UINT64_C(1) ||
        ingress_out.presponse.pvalid != UINT64_C(1) ||
        ingress_out.presponse.pbits != UINT64_C(90) ||
        ingress_out.pstatus != UINT64_C(60))
      fail(1, "nested backward interface flow failed");
  });
}
