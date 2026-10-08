// Simulates ordering and backpressure through a chained Queue and Pipe.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    ingress_in = {.pvalid = UINT64_C(0), .pbits = UINT64_C(0)};
    egress_in = {.pready = UINT64_C(0)};
    tick_model();
    reset = UINT64_C(0);

    ingress_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(161)};
    tick_model();
    ingress_in.pvalid = UINT64_C(0);
    tick_model();
    tick_model();
    CHECK(egress_out.pvalid && egress_out.pbits == UINT64_C(161));

    ingress_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(178)};
    tick_model();
    ingress_in.pbits = UINT64_C(195);
    tick_model();
    ingress_in.pvalid = UINT64_C(0);
    CHECK(egress_out.pvalid && egress_out.pbits == UINT64_C(161));

    egress_in.pready = UINT64_C(1);
    tick_model();
    CHECK(egress_out.pvalid && egress_out.pbits == UINT64_C(178));
    tick_model();
    CHECK(egress_out.pvalid && egress_out.pbits == UINT64_C(195));
    tick_model();
    CHECK(!egress_out.pvalid);
  });
}
