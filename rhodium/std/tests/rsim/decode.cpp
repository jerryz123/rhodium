// Simulates only the output bits constrained by the typed decode relation.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    encoded = UINT64_C(0);
    eval();
    CHECK(alu == UINT64_C(1) && write == UINT64_C(0));

    encoded = UINT64_C(1);
    eval();
    CHECK(alu == UINT64_C(1) && write == UINT64_C(0));

    encoded = UINT64_C(2);
    eval();
    CHECK(alu == UINT64_C(2));

    encoded = UINT64_C(30);
    eval();
    CHECK(alu == UINT64_C(0));
  });
}
