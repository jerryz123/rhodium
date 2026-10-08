// Exercises default FIFO order, full backpressure, count, wraparound, and
// stalls.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void enqueue(std::uint8_t data) {
  ingress_in = {.pvalid = UINT64_C(1), .pbits = data};
  eval();
  CHECK(ingress_out.pready);
  tick_model();
}

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    ingress_in = {.pvalid = UINT64_C(0), .pbits = UINT64_C(0)};
    egress_in = {.pready = UINT64_C(0)};
    tick_model();
    CHECK(!egress_out.pvalid && ingress_out.pready && count == UINT64_C(0));

    reset = UINT64_C(0);
    enqueue(UINT64_C(17));
    CHECK(egress_out.pvalid && egress_out.pbits == UINT64_C(17));
    enqueue(UINT64_C(34));
    enqueue(UINT64_C(51));
    eval();
    CHECK(!ingress_out.pready && egress_out.pbits == UINT64_C(17) &&
          count == UINT64_C(3));

    ingress_in = {.pvalid = UINT64_C(1), .pbits = UINT64_C(68)};
    tick_model();
    CHECK(!ingress_out.pready && egress_out.pbits == UINT64_C(17) &&
          count == UINT64_C(3));

    egress_in.pready = UINT64_C(1);
    eval();
    CHECK(!ingress_out.pready && egress_out.pbits == UINT64_C(17));
    tick_model();
    CHECK(egress_out.pvalid && egress_out.pbits == UINT64_C(34) &&
          ingress_out.pready && count == UINT64_C(2));

    tick_model();
    CHECK(egress_out.pvalid && egress_out.pbits == UINT64_C(51) &&
          count == UINT64_C(2));

    ingress_in.pvalid = UINT64_C(0);
    tick_model();
    CHECK(egress_out.pvalid && egress_out.pbits == UINT64_C(68));
    tick_model();
    CHECK(!egress_out.pvalid && ingress_out.pready && count == UINT64_C(0));
  });
}
