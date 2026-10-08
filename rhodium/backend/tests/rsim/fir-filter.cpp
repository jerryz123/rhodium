// Checks fir-filter behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void check_sample(std::int8_t next_sample, std::int16_t expected) {
  sample = next_sample;
  eval();
  CHECK(filtered == (std::uint64_t(expected) & 8191));
  tick_model();
}

void apply_reset() {
  reset = UINT64_C(1);
  sample = UINT64_C(0);
  tick_model();
  CHECK(filtered == UINT64_C(0));
  reset = UINT64_C(0);
}

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    apply_reset();
    check_sample(UINT64_C(8), UINT64_C(8));
    check_sample(UINT64_C(0), UINT64_C(24));
    check_sample(UINT64_C(0), UINT64_C(24));
    check_sample(UINT64_C(0), UINT64_C(8));
    check_sample(UINT64_C(0), UINT64_C(0));

    apply_reset();
    check_sample(-8, -8);
    check_sample(UINT64_C(0), -24);
    check_sample(UINT64_C(0), -24);
    check_sample(UINT64_C(0), -8);
    check_sample(UINT64_C(0), UINT64_C(0));
  });
}
