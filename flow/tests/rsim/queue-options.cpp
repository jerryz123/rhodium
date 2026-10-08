// Exercises flow-through, piped full replacement, and count queue options.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    ingress_in = {.pvalid = UINT64_C(0), .pbits = UINT64_C(0)};
    egress_in = {.pready = UINT64_C(0)};
    tick_model();
    reset = UINT64_C(0);
    CHECK(count == UINT64_C(0) && !egress_out.pvalid);

    ingress_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(161)};
    egress_in.pready = UINT64_C(1);
    eval();
    CHECK(ingress_out.pready && egress_out.pvalid &&
          egress_out.pbits == UINT64_C(161) && count == UINT64_C(0));
    tick_model();
    ingress_in.pvalid = UINT64_C(0);
    eval();
    CHECK(count == UINT64_C(0) && !egress_out.pvalid);

    egress_in.pready = UINT64_C(0);
    ingress_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(178)};
    tick_model();
    CHECK(count == UINT64_C(1) && egress_out.pvalid &&
          egress_out.pbits == UINT64_C(178));
    ingress_in.pbits = UINT64_C(195);
    tick_model();
    CHECK(count == UINT64_C(2) && !ingress_out.pready);

    ingress_in.pbits = UINT64_C(212);
    egress_in.pready = UINT64_C(1);
    eval();
    CHECK(ingress_out.pready && egress_out.pbits == UINT64_C(178));
    tick_model();
    CHECK(count == UINT64_C(2) && egress_out.pbits == UINT64_C(195));
  });
}
