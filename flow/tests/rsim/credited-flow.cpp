// Simulates initial grants, stalls, credit recycling, ordering, and disabled
// grants.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    grant_enable = UINT64_C(0);
    ingress_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(161)};
    egress_in = {.pready = UINT64_C(0)};
    tick_model();
    reset = UINT64_C(0);

    eval();
    CHECK(!ingress_out.pready && !egress_out.pvalid);
    CHECK(credit_count == 0 && count == 0 && reserved == 0);

    grant_enable = UINT64_C(1);
    eval();
    CHECK(!ingress_out.pready);
    tick_model();
    CHECK(ingress_out.pready);
    CHECK(credit_count == 1 && count == 0 && reserved == 1);

    tick_model();
    ingress_in.pbits = UINT64_C(178);
    tick_model();
    CHECK(!ingress_out.pready);
    CHECK(egress_out.pvalid && egress_out.pbits == UINT64_C(161));

    ingress_in.pbits = UINT64_C(195);
    egress_in.pready = UINT64_C(1);
    eval();
    CHECK(!ingress_out.pready && egress_out.pbits == UINT64_C(161));
    tick_model();
    CHECK(ingress_out.pready && egress_out.pvalid &&
          egress_out.pbits == UINT64_C(178));

    tick_model();
    CHECK(egress_out.pvalid && egress_out.pbits == UINT64_C(195));

    grant_enable = UINT64_C(0);
    ingress_in.pvalid = UINT64_C(0);
    tick_model();
    CHECK(!egress_out.pvalid);

    ingress_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(212)};
    egress_in.pready = UINT64_C(0);
    eval();
    CHECK(ingress_out.pready);
    tick_model();
    CHECK(!ingress_out.pready && egress_out.pvalid &&
          egress_out.pbits == UINT64_C(212));
    CHECK(credit_count == 0 && count == 1 && reserved == 1);

    ingress_in.pvalid = UINT64_C(0);
    egress_in.pready = UINT64_C(1);
    tick_model();
    CHECK(!egress_out.pvalid && !ingress_out.pready);
    CHECK(credit_count == 0 && count == 0 && reserved == 0);
  });
}
