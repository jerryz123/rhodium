// Verifies selected token routing, backpressure, and invalid-selector blocking.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    ports::ingress_in = {.pvalid = UINT64_C(1)};
    ports::egress_0_in = {.pready = UINT64_C(1)};
    ports::egress_1_in = {.pready = UINT64_C(0)};
    ports::egress_2_in = {.pready = UINT64_C(1)};

    ports::select = UINT64_C(0);
    eval();
    CHECK(ports::ingress_out.pready && ports::egress_0_out.pvalid && !ports::egress_1_out.pvalid &&
          !ports::egress_2_out.pvalid);
    ports::select = UINT64_C(1);
    eval();
    CHECK(!ports::ingress_out.pready && ports::egress_1_out.pvalid && !ports::egress_0_out.pvalid &&
          !ports::egress_2_out.pvalid);
    ports::select = UINT64_C(3);
    eval();
    CHECK(!ports::ingress_out.pready && !ports::egress_0_out.pvalid && !ports::egress_1_out.pvalid &&
          !ports::egress_2_out.pvalid);
  });
}
