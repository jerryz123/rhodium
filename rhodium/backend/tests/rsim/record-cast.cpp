// Checks record-cast behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    source = {.pleft = UINT64_C(18), .pright = UINT64_C(52)};
    eval();

    if (bits_out != UINT64_C(4660))
      fail(1, "first record field must occupy the most-significant bits");
    if (restored.pleft != UINT64_C(18) || restored.pright != UINT64_C(52))
      fail(1, "record cast round trip failed");
  });
}
