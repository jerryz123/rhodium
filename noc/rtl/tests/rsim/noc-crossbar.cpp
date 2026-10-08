// Exercises independent destination selection and contention in the linkless
// NoC crossbar.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <random>
std::mt19937 rng(0x1234);

int main() {
  return run_test([] {
    reset = 1;
    ingress_0_in = {};
    ingress_1_in = {};
    ingress_2_in = {};
    egress_0_in.pready = 1;
    egress_1_in.pready = 1;
    egress_2_in.pready = 1;
    tick_model();
    tick_model();
    reset = 0;
    eval();

    CHECK(ingress_0_out.pready && ingress_1_out.pready && ingress_2_out.pready);

    ingress_0_in = {
        .pvalid = UINT64_C(1),
        .pbits = {.proute_ukey = UINT64_C(2), .ppayload = UINT64_C(160)}};
    ingress_1_in = {
        .pvalid = UINT64_C(1),
        .pbits = {.proute_ukey = UINT64_C(3), .ppayload = UINT64_C(177)}};
    ingress_2_in = {
        .pvalid = UINT64_C(1),
        .pbits = {.proute_ukey = UINT64_C(7), .ppayload = UINT64_C(194)}};
    tick_model();
    ingress_0_in.pvalid = 0;
    ingress_1_in.pvalid = 0;
    ingress_2_in.pvalid = 0;
    eval();
    CHECK(egress_0_out.pvalid && egress_0_out.pbits.ppayload == UINT64_C(177));
    CHECK(egress_1_out.pvalid && egress_1_out.pbits.ppayload == UINT64_C(194));
    CHECK(egress_2_out.pvalid && egress_2_out.pbits.ppayload == UINT64_C(160));
    tick_model();

    egress_0_in.pready = 0;
    ingress_0_in = {
        .pvalid = UINT64_C(1),
        .pbits = {.proute_ukey = UINT64_C(0), .ppayload = UINT64_C(164)}};
    ingress_1_in = {
        .pvalid = UINT64_C(1),
        .pbits = {.proute_ukey = UINT64_C(3), .ppayload = UINT64_C(181)}};
    tick_model();
    ingress_0_in.pvalid = 0;
    ingress_1_in.pvalid = 0;
    eval();
    CHECK(egress_0_out.pvalid && egress_0_out.pbits.ppayload == UINT64_C(164));
    egress_0_in.pready = 1;
    tick_model();
    CHECK(egress_0_out.pvalid && egress_0_out.pbits.ppayload == UINT64_C(181));
    tick_model();
    CHECK(!egress_0_out.pvalid);
  });
}
