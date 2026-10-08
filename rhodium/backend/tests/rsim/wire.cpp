// Checks wire behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    left = {0, 1, 0, 1};
    right = {0, 0, 1, 1};
    eval();
    CHECK(pack_bits(sum) == UINT64_C(6));

    left = {1, 1, 1, 1};
    right = {1, 0, 1, 0};
    eval();
    CHECK(pack_bits(sum) == UINT64_C(10));
  });
}
