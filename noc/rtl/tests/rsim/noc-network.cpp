// Verifies end-to-end delivery, destination selection, ordering, and
// conservation under backpressure.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <random>
std::mt19937 rng(0x1234);

int main() {
  return run_test([] {
    int sent;
    int received;
    int cycles;
    int near_expected;
    int far_expected;
    std::uint8_t seen;
    std::uint8_t payload;

    reset = 1;
    injection_in = {};
    ejection_0_in = {};
    ejection_1_in = {};
    tick_model();
    tick_model();
    reset = 0;
    eval();

    CHECK(injection_out.pready);

    sent = 0;
    received = 0;
    cycles = 0;
    near_expected = 0;
    far_expected = 1;
    seen = {};
    while (received < 8 && cycles < 300) {
      eval();
      injection_in.pvalid = sent < 8;
      injection_in.pbits.proute_ukey = (sent & 1);
      injection_in.pbits.ppayload = slice(sent, 7, 0);
      ejection_0_in.pready = ((rng() & 1) == 1);
      ejection_1_in.pready = ((rng() & 1) == 1);

      eval(); // Sample transfers before the active edge.
      if (ejection_0_out.pvalid && ejection_0_in.pready) {
        payload = ejection_0_out.pbits.ppayload;
        CHECK(!(payload & 1));
        CHECK(payload == slice(near_expected, 7, 0));
        CHECK(!((seen >> (payload & 7)) & 1));
        seen |= 1u << (payload & 7);
        near_expected += 2;
        received++;
      }
      if (ejection_1_out.pvalid && ejection_1_in.pready) {
        payload = ejection_1_out.pbits.ppayload;
        CHECK((payload & 1));
        CHECK(payload == slice(far_expected, 7, 0));
        CHECK(!((seen >> (payload & 7)) & 1));
        seen |= 1u << (payload & 7);
        far_expected += 2;
        received++;
      }
      if (injection_in.pvalid && injection_out.pready)
        sent++;
      cycles++;
      tick_model();
    }
    eval();
    injection_in.pvalid = 0;
    ejection_0_in.pready = 0;
    ejection_1_in.pready = 0;

    CHECK(sent == 8);
    CHECK(received == 8 && seen == UINT64_C(255));
    CHECK(near_expected == 8 && far_expected == 9);
  });
}
