// Checks predicate-filter behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void check_case(bool input_valid, std::uint8_t input_bits,
                bool expected_valid) {
  ingress_in.pvalid = input_valid;
  ingress_in.pbits = input_bits;
  eval();
  CHECK(egress_out.pbits == input_bits);
  CHECK(egress_out.pvalid == expected_valid);
}

int main() {
  return run_test([] {
    check_case(UINT64_C(0), UINT64_C(3), UINT64_C(0));
    check_case(UINT64_C(1), UINT64_C(3), UINT64_C(1));
    check_case(UINT64_C(1), UINT64_C(8), UINT64_C(0));
    check_case(UINT64_C(1), UINT64_C(11), UINT64_C(0));
  });
}
