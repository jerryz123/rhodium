// Simulates byte-mask selection on a shared synchronous read-write memory port.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void read_and_expect(std::uint32_t expected) {
  write = UINT64_C(0);
  tick_model();
  CHECK(read_data == expected);
}

int main() {
  return run_test([] {
    reset = UINT64_C(0);
    address = UINT64_C(1);
    enable = UINT64_C(1);
    write = UINT64_C(1);
    write_data = UINT64_C(2864434397);
    write_mask = UINT64_C(15);
    tick_model();
    read_and_expect(UINT64_C(2864434397));

    write = UINT64_C(1);
    write_data = UINT64_C(287454020);
    write_mask = UINT64_C(5);
    tick_model();
    read_and_expect(UINT64_C(2854407236));

    write = UINT64_C(1);
    write_data = UINT64_C(4294967295);
    write_mask = UINT64_C(0);
    tick_model();
    read_and_expect(UINT64_C(2854407236));

    write = UINT64_C(1);
    write_data = UINT64_C(1432778632);
    write_mask = UINT64_C(15);
    tick_model();
    read_and_expect(UINT64_C(1432778632));

    enable = UINT64_C(0);
  });
}
