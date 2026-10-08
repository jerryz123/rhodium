// Exercises simple-router matching, buffering, ejection, and backpressure.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <random>
std::mt19937 rng(0x1234);

int main() {
  return run_test([] {
    int sent0;
    int sent1;
    int received;
    int cycles;
    std::uint8_t seen;
    std::uint8_t payload;

    reset = 1;
    middle_ingress_0_in = {};
    middle_ingress_1_in = {};
    middle_egress_0_in = {};
    middle_egress_1_in = {};
    destination_ingress_0_in = {};
    destination_ingress_1_in = {};
    destination_egress_0_in = {};
    tick_model();
    tick_model();
    reset = 0;
    eval();

    CHECK(middle_ingress_0_out.pready && middle_ingress_1_out.pready);
    CHECK(destination_ingress_0_out.pready && destination_ingress_1_out.pready);

    middle_ingress_0_in = {
        .pvalid = UINT64_C(1),
        .pbits = {.proute_ukey = UINT64_C(0), .ppayload = UINT64_C(161)}};
    middle_ingress_1_in = {
        .pvalid = UINT64_C(1),
        .pbits = {.proute_ukey = UINT64_C(0), .ppayload = UINT64_C(178)}};
    tick_model();
    middle_ingress_0_in.pvalid = 0;
    middle_ingress_1_in.pvalid = 0;
    eval();
    CHECK(middle_egress_0_out.pvalid && middle_egress_1_out.pvalid);
    CHECK(middle_egress_0_out.pbits.ppayload == UINT64_C(161));
    CHECK(middle_egress_1_out.pbits.ppayload == UINT64_C(178));

    middle_egress_1_in.pready = 1;
    tick_model();
    CHECK(middle_egress_0_out.pvalid &&
          middle_egress_0_out.pbits.ppayload == UINT64_C(161));
    CHECK(!middle_egress_1_out.pvalid && middle_ingress_1_out.pready);
    middle_egress_0_in.pready = 1;
    tick_model();
    CHECK(!middle_egress_0_out.pvalid && middle_ingress_0_out.pready);
    middle_egress_0_in.pready = 0;
    middle_egress_1_in.pready = 0;

    destination_ingress_0_in = {
        .pvalid = UINT64_C(1),
        .pbits = {.proute_ukey = UINT64_C(0), .ppayload = UINT64_C(195)}};
    destination_ingress_1_in = {
        .pvalid = UINT64_C(1),
        .pbits = {.proute_ukey = UINT64_C(0), .ppayload = UINT64_C(212)}};
    tick_model();
    destination_ingress_0_in.pvalid = 0;
    destination_ingress_1_in.pvalid = 0;
    eval();
    CHECK(destination_egress_0_out.pvalid &&
          destination_egress_0_out.pbits.ppayload == UINT64_C(195));
    destination_egress_0_in.pready = 1;
    tick_model();
    CHECK(destination_egress_0_out.pvalid &&
          destination_egress_0_out.pbits.ppayload == UINT64_C(212));
    tick_model();
    CHECK(!destination_egress_0_out.pvalid);

    sent0 = 0;
    sent1 = 0;
    received = 0;
    cycles = 0;
    seen = {};
    while (received < 8 && cycles < 200) {
      eval();
      destination_egress_0_in.pready = ((rng() & 1) == 1);
      destination_ingress_0_in.pvalid = sent0 < 4;
      destination_ingress_0_in.pbits.proute_ukey = 0;
      destination_ingress_0_in.pbits.ppayload = (16 | ((sent0 & 3) << 1) | 0);
      destination_ingress_1_in.pvalid = sent1 < 4;
      destination_ingress_1_in.pbits.proute_ukey = 0;
      destination_ingress_1_in.pbits.ppayload = (16 | ((sent1 & 3) << 1) | 1);

      eval(); // Sample transfers before the active edge.
      if (destination_egress_0_out.pvalid && destination_egress_0_in.pready) {
        payload = destination_egress_0_out.pbits.ppayload;
        CHECK(payload >= UINT64_C(16) && payload <= UINT64_C(23));
        CHECK(!((seen >> (payload & 7)) & 1));
        seen |= 1u << (payload & 7);
        received++;
      }
      if (destination_ingress_0_in.pvalid && destination_ingress_0_out.pready)
        sent0++;
      if (destination_ingress_1_in.pvalid && destination_ingress_1_out.pready)
        sent1++;
      cycles++;
      tick_model();
    }
    eval();
    destination_ingress_0_in.pvalid = 0;
    destination_ingress_1_in.pvalid = 0;
    destination_egress_0_in.pready = 0;

    CHECK(received == 8 && seen == UINT64_C(255));
    CHECK(sent0 == 4 && sent1 == 4);
  });
}
