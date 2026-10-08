// Checks multi-write-memory behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    read_address = UINT64_C(0);
    first_address = UINT64_C(0);
    second_address = UINT64_C(1);
    first_data = UINT64_C(165);
    second_data = UINT64_C(90);
    first_enable = UINT64_C(1);
    second_enable = UINT64_C(1);

    tick_model();
    first_enable = UINT64_C(0);
    second_enable = UINT64_C(0);
    CHECK(read_data == UINT64_C(165));

    read_address = UINT64_C(1);
    eval();
    CHECK(read_data == UINT64_C(90));

    first_address = UINT64_C(2);
    first_data = UINT64_C(60);
    first_enable = UINT64_C(1);
    tick_model();
    first_enable = UINT64_C(0);
    read_address = UINT64_C(2);
    eval();
    CHECK(read_data == UINT64_C(60));
  });
}
