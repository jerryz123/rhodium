// Verifies fair changed-state delivery, irrevocable stalls, and local state
// replication.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    desired[0] = {.penabled = UINT64_C(0), .pmode = UINT64_C(0)};
    desired[1] = {.penabled = UINT64_C(0), .pmode = UINT64_C(0)};
    desired[2] = {.penabled = UINT64_C(0), .pmode = UINT64_C(0)};
    changes_in = {.pready = UINT64_C(0)};
    replica_update_in = {
        .pvalid = UINT64_C(0),
        .pbits = {.penabled = UINT64_C(0), .pmode = UINT64_C(0)}};
    tick_model();
    reset = UINT64_C(0);
    tick_model();
    CHECK(!changes_out.pvalid && !replica.penabled &&
          replica.pmode == UINT64_C(0));
    CHECK(replica_update_out.pready);

    desired[0] = {.penabled = UINT64_C(1), .pmode = UINT64_C(1)};
    desired[1] = {.penabled = UINT64_C(0), .pmode = UINT64_C(2)};
    tick_model();
    CHECK(changes_out.pvalid && changes_out.pbits.pindex == UINT64_C(0) &&
          changes_out.pbits.pvalue.penabled &&
          changes_out.pbits.pvalue.pmode == UINT64_C(1));

    desired[0] = {.penabled = UINT64_C(1), .pmode = UINT64_C(3)};
    tick_model();
    CHECK(changes_out.pvalid && changes_out.pbits.pindex == UINT64_C(0) &&
          changes_out.pbits.pvalue.penabled &&
          changes_out.pbits.pvalue.pmode == UINT64_C(1));

    changes_in.pready = UINT64_C(1);
    tick_model();
    CHECK(changes_out.pvalid && changes_out.pbits.pindex == UINT64_C(1) &&
          !changes_out.pbits.pvalue.penabled &&
          changes_out.pbits.pvalue.pmode == UINT64_C(2));
    tick_model();
    CHECK(changes_out.pvalid && changes_out.pbits.pindex == UINT64_C(0) &&
          changes_out.pbits.pvalue.penabled &&
          changes_out.pbits.pvalue.pmode == UINT64_C(3));
    tick_model();
    CHECK(!changes_out.pvalid);

    desired[0] = {.penabled = UINT64_C(0), .pmode = UINT64_C(0)};
    tick_model();
    CHECK(changes_out.pvalid && changes_out.pbits.pindex == UINT64_C(0) &&
          !changes_out.pbits.pvalue.penabled &&
          changes_out.pbits.pvalue.pmode == UINT64_C(0));
    tick_model();

    replica_update_in = {
        .pvalid = UINT64_C(1),
        .pbits = {.penabled = UINT64_C(1), .pmode = UINT64_C(5)}};
    tick_model();
    replica_update_in.pvalid = UINT64_C(0);
    CHECK(replica.penabled && replica.pmode == UINT64_C(5));
  });
}
