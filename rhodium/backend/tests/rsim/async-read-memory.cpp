// Checks async-read-memory behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    read_address = UINT64_C(1);
    write_address = UINT64_C(1);
    write_data = UINT64_C(165);
    write_enable = UINT64_C(1);
    tick_model();
    CHECK(read_data == UINT64_C(165));

    write_enable = UINT64_C(0);
    write_data = UINT64_C(60);
    tick_model();
    CHECK(read_data == UINT64_C(165));

    write_address = UINT64_C(2);
    write_data = UINT64_C(90);
    write_enable = UINT64_C(1);
    tick_model();
    CHECK(read_data == UINT64_C(165));

    read_address = UINT64_C(2);
    eval();
    CHECK(read_data == UINT64_C(90));
  });
}
