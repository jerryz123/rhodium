// Checks family-site routing, generic physical-link binding, and downstream
// backpressure.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <random>
std::mt19937 rng(0x1234);

void send_source(std::uint8_t route_key, std::uint8_t payload) {
  eval();
  source_ingress_0_in = {
      .pvalid = UINT64_C(1),
      .pbits = {.proute_ukey = route_key, .ppayload = payload}};
  eval();
  while (!(source_ingress_0_out.pready)) {
    tick_model();
  }
  tick_model();
  eval();
  source_ingress_0_in.pvalid = 0;
}

void send_hub_local(std::uint8_t payload) {
  eval();
  hub_ingress_0_in = {
      .pvalid = UINT64_C(1),
      .pbits = {.proute_ukey = UINT64_C(2), .ppayload = payload}};
  eval();
  while (!(hub_ingress_0_out.pready)) {
    tick_model();
  }
  tick_model();
  eval();
  hub_ingress_0_in.pvalid = 0;
}

void send_hub_transit(std::uint8_t payload) {
  eval();
  hub_ingress_1_in = {
      .pvalid = UINT64_C(1),
      .pbits = {.proute_ukey = UINT64_C(0), .ppayload = payload}};
  eval();
  while (!(hub_ingress_1_out.pready)) {
    tick_model();
  }
  tick_model();
  eval();
  hub_ingress_1_in.pvalid = 0;
}

int main() {
  return run_test([] {
    reset = 1;
    source_ingress_0_in = {};
    source_ingress_1_in = {};
    source_ingress_2_in = {};
    source_egress_0_in = {.pready = UINT64_C(1)};
    source_egress_1_in = {.pready = UINT64_C(1)};
    hub_ingress_0_in = {};
    hub_ingress_1_in = {};
    hub_ingress_2_in = {};
    hub_egress_0_in = {.pready = UINT64_C(1)};
    hub_egress_1_in = {.pready = UINT64_C(1)};
    tick_model();
    tick_model();
    reset = 0;

    send_source(UINT64_C(0), UINT64_C(160));
    eval();
    while (!(source_egress_0_out.pvalid))
      tick_model();
    CHECK(source_egress_0_out.pbits.ppayload == UINT64_C(160) &&
          !source_egress_1_out.pvalid);
    tick_model();

    send_hub_local(UINT64_C(176));
    eval();
    while (!(hub_egress_1_out.pvalid))
      tick_model();
    CHECK(hub_egress_1_out.pbits.ppayload == UINT64_C(176) &&
          !hub_egress_0_out.pvalid);
    tick_model();

    send_hub_transit(UINT64_C(192));
    eval();
    while (!(hub_egress_0_out.pvalid))
      tick_model();
    CHECK(hub_egress_0_out.pbits.ppayload == UINT64_C(192) &&
          !hub_egress_1_out.pvalid);
    tick_model();

    eval();
    source_egress_0_in.pready = 0;
    send_source(UINT64_C(0), UINT64_C(208));
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index) {
      tick_model();
      CHECK(!source_egress_1_out.pvalid);
    }
    eval();
    source_egress_0_in.pready = 1;
    eval();
    eval();
    while (!(source_egress_0_out.pvalid))
      tick_model();
    CHECK(source_egress_0_out.pbits.ppayload == UINT64_C(208));
    tick_model();
  });
}
