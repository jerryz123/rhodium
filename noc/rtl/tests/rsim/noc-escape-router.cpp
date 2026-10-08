// Exercises immediate adaptive routing, persistent escape fallback, and fair
// contention.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <random>
std::mt19937 rng(0x1234);

void offer_0(std::uint8_t payload) {
  eval();
  ingress_0_in = {.pvalid = UINT64_C(1),
                  .pbits = {.proute_ukey = UINT64_C(0), .ppayload = payload}};
  eval();
  while (!(ingress_0_out.pready)) {
    tick_model();
  }
  tick_model();
  eval();
  ingress_0_in.pvalid = 0;
}

int main() {
  return run_test([] {
    reset = 1;
    ingress_0_in = {};
    ingress_1_in = {};
    egress_0_in = {};
    egress_1_in = {};
    tick_model();
    tick_model();
    reset = 0;

    egress_0_in.pready = 1;
    offer_0(UINT64_C(160));
    eval();
    CHECK(egress_0_out.pvalid && egress_0_out.pbits.ppayload == UINT64_C(160) &&
          !egress_1_out.pvalid);
    tick_model();
    egress_0_in.pready = 0;

    offer_0(UINT64_C(176));
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
      eval();
      CHECK(!egress_0_out.pvalid && egress_1_out.pvalid &&
            egress_1_out.pbits.ppayload == UINT64_C(176));
      tick_model();
    }
    egress_1_in.pready = 1;
    tick_model();
    egress_1_in.pready = 0;

    reset = 1;
    tick_model();
    reset = 0;
    ingress_0_in = {
        .pvalid = UINT64_C(1),
        .pbits = {.proute_ukey = UINT64_C(0), .ppayload = UINT64_C(192)}};
    ingress_1_in = {
        .pvalid = UINT64_C(1),
        .pbits = {.proute_ukey = UINT64_C(0), .ppayload = UINT64_C(208)}};
    tick_model();
    ingress_0_in.pvalid = 0;
    ingress_1_in.pvalid = 0;
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index) {
      CHECK(egress_1_out.pvalid &&
            egress_1_out.pbits.ppayload == UINT64_C(192));
      tick_model();
    }
    egress_1_in.pready = 1;
    tick_model();
    CHECK(egress_1_out.pvalid && egress_1_out.pbits.ppayload == UINT64_C(208));
    tick_model();
    CHECK(!egress_1_out.pvalid);
  });
}
