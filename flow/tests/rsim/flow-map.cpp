// Verifies block payload mapping and unchanged ready-valid control.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    ingress_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(165)};
    tag = UINT64_C(12);
    egress_in = {.pready = UINT64_C(0)};
    eval();
    CHECK(!ingress_out.pready && egress_out.pvalid &&
          egress_out.pbits.pdata == UINT64_C(165) &&
          egress_out.pbits.ptag == UINT64_C(12));

    egress_in.pready = UINT64_C(1);
    eval();
    CHECK(ingress_out.pready && egress_out.pvalid &&
          egress_out.pbits.pdata == UINT64_C(165) &&
          egress_out.pbits.ptag == UINT64_C(12));

    ingress_in.pvalid = UINT64_C(0);
    tag = UINT64_C(3);
    eval();
    CHECK(ingress_out.pready && !egress_out.pvalid &&
          egress_out.pbits.pdata == UINT64_C(165) &&
          egress_out.pbits.ptag == UINT64_C(3));
  });
}
