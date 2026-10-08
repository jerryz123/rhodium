// Checks unsigned-comparisons behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void check_results(std::uint8_t next_a, std::uint8_t next_b, bool expected_lt,
                   bool expected_gt, bool expected_le, bool expected_ge) {
  a = next_a;
  b = next_b;
  eval();
  CHECK(lt == expected_lt);
  CHECK(gt == expected_gt);
  CHECK(le == expected_le);
  CHECK(ge == expected_ge);
}

int main() {
  return run_test([] {
    check_results(UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
                  UINT64_C(1), UINT64_C(1));
    check_results(UINT64_C(0), UINT64_C(255), UINT64_C(1), UINT64_C(0),
                  UINT64_C(1), UINT64_C(0));
    check_results(UINT64_C(255), UINT64_C(0), UINT64_C(0), UINT64_C(1),
                  UINT64_C(0), UINT64_C(1));
    check_results(UINT64_C(127), UINT64_C(128), UINT64_C(1), UINT64_C(0),
                  UINT64_C(1), UINT64_C(0));
  });
}
