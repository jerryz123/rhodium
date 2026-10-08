// Exercises latency, throughput, and backpressure in a two-stage token pipe.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    ingress_in = {.pvalid = UINT64_C(0)};
    egress_in = {.pready = UINT64_C(0)};
    tick_model();
    CHECK(!egress_out.pvalid && ingress_out.pready);

    reset = UINT64_C(0);
    egress_in.pready = UINT64_C(1);
    ingress_in.pvalid = UINT64_C(1);
    tick_model();
    CHECK(!egress_out.pvalid);
    tick_model();
    CHECK(egress_out.pvalid);

    egress_in.pready = UINT64_C(0);
    eval();
    CHECK(!ingress_out.pready && egress_out.pvalid);
    tick_model();
    CHECK(egress_out.pvalid);

    ingress_in.pvalid = UINT64_C(0);
    egress_in.pready = UINT64_C(1);
    tick_model();
    CHECK(egress_out.pvalid);
    tick_model();
    CHECK(!egress_out.pvalid);
  });
}
