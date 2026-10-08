// Checks that fixed-flit framing advances on transfers rather than clock
// cycles.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void expect_flit(bool expected_first, bool expected_last,
                 std::uint8_t expected_payload) {
  CHECK(egress_out.pvalid && egress_out.pbits.pfirst == expected_first &&
        egress_out.pbits.plast == expected_last &&
        egress_out.pbits.ppayload == expected_payload);
}

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    ingress_in = {.pvalid = UINT64_C(0), .pbits = {.ppayload = UINT64_C(0)}};
    egress_in = {.pready = UINT64_C(0)};
    tick_model();
    reset = UINT64_C(0);

    ingress_in = {.pvalid = UINT64_C(1), .pbits = {.ppayload = UINT64_C(161)}};
    eval();
    expect_flit(UINT64_C(1), UINT64_C(0), UINT64_C(161));
    tick_model();
    expect_flit(UINT64_C(1), UINT64_C(0), UINT64_C(161));

    egress_in.pready = UINT64_C(1);
    tick_model();
    ingress_in.pbits.ppayload = UINT64_C(178);
    eval();
    expect_flit(UINT64_C(0), UINT64_C(0), UINT64_C(178));

    tick_model();
    ingress_in.pbits.ppayload = UINT64_C(195);
    eval();
    expect_flit(UINT64_C(0), UINT64_C(1), UINT64_C(195));

    egress_in.pready = UINT64_C(0);
    tick_model();
    expect_flit(UINT64_C(0), UINT64_C(1), UINT64_C(195));

    egress_in.pready = UINT64_C(1);
    tick_model();
    ingress_in.pbits.ppayload = UINT64_C(212);
    eval();
    expect_flit(UINT64_C(1), UINT64_C(0), UINT64_C(212));
  });
}
