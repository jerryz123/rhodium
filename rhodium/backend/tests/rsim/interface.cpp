// Checks interface behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    ingress_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(165)};
    egress_in = {.pready = UINT64_C(1)};
    eval();

    if (egress_out.pvalid != UINT64_C(1) || egress_out.pbits != UINT64_C(165))
      fail(1, "forward interface flow failed");
    if (ingress_out.pready != UINT64_C(1))
      fail(1, "backward interface flow failed");
  });
}
