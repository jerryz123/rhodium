// Checks multiply behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    a = UINT64_C(7);
    b = UINT64_C(9);
    eval();
    CHECK(product == UINT64_C(63));

    a = UINT64_C(200);
    b = UINT64_C(3);
    eval();
    CHECK(product == UINT64_C(88));

    a = UINT64_C(255);
    b = UINT64_C(255);
    eval();
    CHECK(product == UINT64_C(1));
  });
}
