// Verifies packet ownership across transfers, bubbles, stalls, and final flits.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    ingress_0_in = {.pvalid = UINT64_C(1),
                    .pbits = {.phead = UINT64_C(1),
                              .ptail = UINT64_C(0),
                              .ppayload = UINT64_C(160)}};
    ingress_1_in = {.pvalid = UINT64_C(1),
                    .pbits = {.phead = UINT64_C(1),
                              .ptail = UINT64_C(1),
                              .ppayload = UINT64_C(176)}};
    ingress_2_in = {.pvalid = UINT64_C(1),
                    .pbits = {.phead = UINT64_C(1),
                              .ptail = UINT64_C(1),
                              .ppayload = UINT64_C(192)}};
    egress_in = {.pready = UINT64_C(1)};
    tick_model();
    reset = UINT64_C(0);

    eval();
    CHECK(egress_out.pvalid && egress_out.pbits.ppayload == UINT64_C(160) &&
          ingress_0_out.pready && !ingress_1_out.pready &&
          !ingress_2_out.pready);
    tick_model();

    ingress_0_in.pvalid = UINT64_C(0);
    eval();
    CHECK(!egress_out.pvalid && ingress_0_out.pready && !ingress_1_out.pready &&
          !ingress_2_out.pready);
    tick_model();

    ingress_0_in = {.pvalid = UINT64_C(1),
                    .pbits = {.phead = UINT64_C(0),
                              .ptail = UINT64_C(0),
                              .ppayload = UINT64_C(161)}};
    tick_model();
    CHECK(egress_out.pvalid && egress_out.pbits.ppayload == UINT64_C(161) &&
          ingress_0_out.pready);

    ingress_0_in.pbits = {
        .phead = UINT64_C(0), .ptail = UINT64_C(1), .ppayload = UINT64_C(162)};
    egress_in.pready = UINT64_C(0);
    eval();
    CHECK(egress_out.pvalid && egress_out.pbits.ppayload == UINT64_C(162) &&
          !ingress_0_out.pready && !ingress_1_out.pready);
    tick_model();

    egress_in.pready = UINT64_C(1);
    tick_model();
    CHECK(egress_out.pvalid && egress_out.pbits.ppayload == UINT64_C(176) &&
          ingress_1_out.pready && !ingress_0_out.pready);
    tick_model();
    CHECK(egress_out.pvalid && egress_out.pbits.ppayload == UINT64_C(192) &&
          ingress_2_out.pready);
  });
}
