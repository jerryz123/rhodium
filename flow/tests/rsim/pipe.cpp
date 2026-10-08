// Exercises latency, throughput, and backpressure stability in a two-stage
// pipe.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    ingress_in = {.pvalid = UINT64_C(0), .pbits = UINT64_C(0)};
    egress_in = {.pready = UINT64_C(0)};
    tick_model();
    CHECK(!egress_out.pvalid && ingress_out.pready);

    reset = UINT64_C(0);
    egress_in.pready = UINT64_C(1);
    ingress_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(161)};
    tick_model();
    CHECK(!egress_out.pvalid);

    ingress_in.pbits = UINT64_C(178);
    tick_model();
    CHECK(egress_out.pvalid && egress_out.pbits == UINT64_C(161));

    egress_in.pready = UINT64_C(0);
    ingress_in.pbits = UINT64_C(195);
    eval();
    CHECK(!ingress_out.pready);
    tick_model();
    CHECK(egress_out.pvalid && egress_out.pbits == UINT64_C(161));

    egress_in.pready = UINT64_C(1);
    eval();
    CHECK(ingress_out.pready);
    tick_model();
    CHECK(egress_out.pvalid && egress_out.pbits == UINT64_C(178));

    ingress_in.pvalid = UINT64_C(0);
    tick_model();
    CHECK(egress_out.pvalid && egress_out.pbits == UINT64_C(195));
    tick_model();
    CHECK(!egress_out.pvalid);
  });
}
