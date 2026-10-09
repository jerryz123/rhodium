// Checks bundle behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = 0;
    ports::select = 0;
    direct = {.pleft = UINT64_C(18), .pright = UINT64_C(52)};
    alternate = {.pleft = UINT64_C(86), .pright = UINT64_C(120)};

    reset = 1;
    tick_model();
    reset = 0;
    if (result.pleft != UINT64_C(0) || result.pright != UINT64_C(0))
      fail(1, "record reset failed");

    ports::select = 1;
    tick_model();
    if (result.pleft != UINT64_C(18) || result.pright != UINT64_C(52))
      fail(1, "record true selection failed");

    ports::select = 0;
    tick_model();
    if (result.pleft != UINT64_C(86) || result.pright != UINT64_C(120))
      fail(1, "record false selection failed");
  });
}
