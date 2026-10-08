// Checks one-hot behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void check(std::uint8_t value, std::uint8_t expected_next, bool expected_last) {
  current = value;
  eval();
  CHECK(current_bits == value);
  CHECK(next_grant == expected_next);
  CHECK(is_last == expected_last);
}

int main() {
  return run_test([] {
    check(UINT64_C(1), UINT64_C(2), UINT64_C(0));
    check(UINT64_C(2), UINT64_C(4), UINT64_C(0));
    check(UINT64_C(4), UINT64_C(8), UINT64_C(0));
    check(UINT64_C(8), UINT64_C(1), UINT64_C(1));
  });
}
