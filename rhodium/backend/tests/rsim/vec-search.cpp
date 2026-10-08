// Checks vec-search behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void check_next(std::uint8_t expected) {
  {
    tick_model();
    CHECK(out == expected);
  }
}

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    CHECK(out == UINT64_C(0));

    reset = UINT64_C(0);
    check_next(UINT64_C(4));
    check_next(UINT64_C(15));
    check_next(UINT64_C(14));
    check_next(UINT64_C(2));
    check_next(UINT64_C(5));
    check_next(UINT64_C(13));
    check_next(UINT64_C(0));
    check_next(UINT64_C(0));
  });
}
