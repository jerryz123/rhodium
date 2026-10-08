// Exercises rotating fairness and selected readiness in the token arbiter.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    ingress_0_in = {.pvalid = UINT64_C(1)};
    ingress_1_in = {.pvalid = UINT64_C(1)};
    ingress_2_in = {.pvalid = UINT64_C(1)};
    egress_in = {.pready = UINT64_C(0)};
    tick_model();
    reset = UINT64_C(0);
    CHECK(chosen == UINT64_C(0) && !ingress_0_out.pready);

    egress_in.pready = UINT64_C(1);
    tick_model();
    CHECK(chosen == UINT64_C(1) && ingress_1_out.pready);
    tick_model();
    CHECK(chosen == UINT64_C(2) && ingress_2_out.pready);
    tick_model();
    CHECK(chosen == UINT64_C(0) && ingress_0_out.pready);
  });
}
