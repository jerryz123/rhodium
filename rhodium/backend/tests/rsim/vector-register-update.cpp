// Checks vector-register-update behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    reset = UINT64_C(1);
    write_enable = UINT64_C(0);
    tick_model();
    CHECK((result[0] == 0 && result[1] == 0 && result[2] == 0));

    reset = UINT64_C(0);
    write_enable = UINT64_C(1);
    selector = UINT64_C(1);
    replacement = UINT64_C(9);
    tick_model();
    CHECK(result[0] == UINT64_C(0) && result[1] == UINT64_C(9) &&
          result[2] == UINT64_C(0));

    write_enable = UINT64_C(0);
    selector = UINT64_C(2);
    replacement = UINT64_C(5);
    tick_model();
    CHECK(result[0] == UINT64_C(0) && result[1] == UINT64_C(9) &&
          result[2] == UINT64_C(0));

    write_enable = UINT64_C(1);
    tick_model();
    CHECK(result[0] == UINT64_C(0) && result[1] == UINT64_C(9) &&
          result[2] == UINT64_C(5));
  });
}
