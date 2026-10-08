// Checks always-capture payload latency through bubbles, back-to-back tokens,
// and reset.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
std::remove_reference_t<decltype(ingress_in)> previous_input{};
bool previous_reset = false;
int samples = 0;

void sample(bool rst, bool valid, std::uint8_t bits) {

  reset = rst;
  ingress_in = {.pvalid = valid, .pbits = bits};
  tick_model();
  if (samples != 0) {
    CHECK(egress_out.pvalid ==
          (!rst && !previous_reset && previous_input.pvalid));
    CHECK(egress_out.pbits == previous_input.pbits);
  }
  previous_input = ingress_in;
  previous_reset = rst;
  samples++;
}

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    ingress_in = {};
    sample(1, 0, UINT64_C(1));
    sample(0, 1, UINT64_C(161));
    sample(0, 1, UINT64_C(178));
    sample(0, 0, UINT64_C(51));
    sample(0, 0, UINT64_C(68));
    sample(0, 1, UINT64_C(195));
    sample(1, 1, UINT64_C(212));
    sample(0, 0, UINT64_C(85));
    sample(0, 1, UINT64_C(229));
    sample(0, 0, UINT64_C(102));
    sample(0, 0, UINT64_C(119));
  });
}
