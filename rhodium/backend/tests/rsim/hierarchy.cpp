// Checks hierarchy behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    a = UINT64_C(7);
    b = UINT64_C(11);
    c = UINT64_C(20);
    eval();
    CHECK(sum0 == UINT64_C(18));
    CHECK(sum1 == UINT64_C(27));

    a = UINT64_C(250);
    b = UINT64_C(10);
    c = UINT64_C(20);
    eval();
    CHECK(sum0 == UINT64_C(4));
    CHECK(sum1 == UINT64_C(14));
  });
}
