// Verifies selected token routing, backpressure, and invalid-selector blocking.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    ingress_in = {.pvalid = UINT64_C(1)};
    egress_0_in = {.pready = UINT64_C(1)};
    egress_1_in = {.pready = UINT64_C(0)};
    egress_2_in = {.pready = UINT64_C(1)};

    select = UINT64_C(0);
    eval();
    CHECK(ingress_out.pready && egress_0_out.pvalid && !egress_1_out.pvalid &&
          !egress_2_out.pvalid);
    select = UINT64_C(1);
    eval();
    CHECK(!ingress_out.pready && egress_1_out.pvalid && !egress_0_out.pvalid &&
          !egress_2_out.pvalid);
    select = UINT64_C(3);
    eval();
    CHECK(!ingress_out.pready && !egress_0_out.pvalid && !egress_1_out.pvalid &&
          !egress_2_out.pvalid);
  });
}
