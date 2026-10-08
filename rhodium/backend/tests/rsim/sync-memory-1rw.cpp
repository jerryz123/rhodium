// Checks sync-memory-1rw behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    reset = UINT64_C(0);
    address = UINT64_C(1);
    enable = UINT64_C(1);
    write = UINT64_C(1);
    write_data = UINT64_C(165);
    tick_model();

    write = UINT64_C(0);
    tick_model();
    CHECK(read_data == UINT64_C(165));

    address = UINT64_C(2);
    write = UINT64_C(1);
    write_data = UINT64_C(90);
    tick_model();

    write = UINT64_C(0);
    tick_model();
    CHECK(read_data == UINT64_C(90));

    enable = UINT64_C(0);
    tick_model();
  });
}
