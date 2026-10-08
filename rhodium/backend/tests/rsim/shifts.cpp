// Checks shifts behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void check_shifts(std::uint8_t value_in, std::uint8_t amount_in,
                  std::uint16_t wide_amount_in, std::uint8_t expected_left,
                  std::uint8_t expected_right, std::uint8_t expected_left_wide,
                  std::uint8_t expected_right_wide) {
  value = value_in;
  amount = amount_in;
  wide_amount = wide_amount_in;
  eval();
  CHECK(left == expected_left);
  CHECK(right == expected_right);
  CHECK(left_wide == expected_left_wide);
  CHECK(right_wide == expected_right_wide);
  CHECK(left_three == ((value_in << 3) & 255));
  CHECK(right_three == value_in >> 3);
}

int main() {
  return run_test([] {
    check_shifts(UINT64_C(129), UINT64_C(0), UINT64_C(0), UINT64_C(129),
                 UINT64_C(129), UINT64_C(129), UINT64_C(129));
    check_shifts(UINT64_C(129), UINT64_C(1), UINT64_C(1), UINT64_C(2),
                 UINT64_C(64), UINT64_C(2), UINT64_C(64));
    check_shifts(UINT64_C(129), UINT64_C(3), UINT64_C(7), UINT64_C(8),
                 UINT64_C(16), UINT64_C(128), UINT64_C(1));
    check_shifts(UINT64_C(255), UINT64_C(7), UINT64_C(8), UINT64_C(128),
                 UINT64_C(1), UINT64_C(0), UINT64_C(0));
    check_shifts(UINT64_C(255), UINT64_C(4), UINT64_C(4095), UINT64_C(240),
                 UINT64_C(15), UINT64_C(0), UINT64_C(0));
  });
}
