// Checks width-ops behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void check_outputs(std::uint8_t a_value, std::uint8_t b_value,
                   std::uint16_t expected_joined, std::uint8_t expected_middle,
                   std::uint16_t expected_widened,
                   std::uint8_t expected_narrowed) {
  a = a_value;
  b = b_value;
  eval();
  CHECK(joined == expected_joined);
  CHECK(middle == expected_middle);
  CHECK(widened == expected_widened);
  CHECK(narrowed == expected_narrowed);
}

int main() {
  return run_test([] {
    check_outputs(UINT64_C(214), UINT64_C(10), UINT64_C(3434), UINT64_C(5),
                  UINT64_C(214), UINT64_C(6));
    check_outputs(UINT64_C(60), UINT64_C(5), UINT64_C(965), UINT64_C(15),
                  UINT64_C(60), UINT64_C(12));
  });
}
