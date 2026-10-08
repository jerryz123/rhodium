// Checks sync-memory behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    reset = UINT64_C(0);
    read_address = UINT64_C(0);
    read_enable = UINT64_C(0);
    write_address = UINT64_C(1);
    write_data = UINT64_C(165);
    write_enable = UINT64_C(1);
    tick_model();

    write_enable = UINT64_C(0);
    read_address = UINT64_C(1);
    read_enable = UINT64_C(1);
    tick_model();
    CHECK(read_data == UINT64_C(165));

    read_address = UINT64_C(2);
    eval();
    CHECK(read_data == UINT64_C(165));

    write_address = UINT64_C(2);
    write_data = UINT64_C(90);
    write_enable = UINT64_C(1);
    read_enable = UINT64_C(0);
    tick_model();

    write_enable = UINT64_C(0);
    read_enable = UINT64_C(1);
    tick_model();
    CHECK(read_data == UINT64_C(90));
  });
}
