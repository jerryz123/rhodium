// Checks expanding-arithmetic behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void check_results(std::uint8_t next_a, std::uint8_t next_b,
                   std::int8_t next_signed_a, std::int8_t next_signed_b,
                   std::uint16_t expected_sum, std::uint16_t expected_product,
                   std::int16_t expected_signed_product) {
  a = next_a;
  b = next_b;
  signed_a = next_signed_a;
  signed_b = next_signed_b;
  eval();
  CHECK(sum == expected_sum);
  CHECK(product == expected_product);
  CHECK(signed_product == (std::uint64_t(expected_signed_product) & 4095));
}

int main() {
  return run_test([] {
    check_results(UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
                  UINT64_C(0), UINT64_C(0), UINT64_C(0));
    check_results(UINT64_C(255), UINT64_C(15), -128, UINT64_C(7), UINT64_C(270),
                  UINT64_C(3825), -896);
    check_results(UINT64_C(128), UINT64_C(8), UINT64_C(127), -8, UINT64_C(136),
                  UINT64_C(1024), -1016);
  });
}
