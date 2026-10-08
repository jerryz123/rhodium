// Checks every response opcode against independent milestone expectations.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    for (int op = 0; op < 32; op++) {
      std::uint8_t expected;
      opcode = ((op)&low_mask(5));
      expected = op == 6 || op == 14 ? 1 : op == 4 ? 2 : op == 5 ? 3 : 0;
      eval();
      CHECK(effects == expected && dbid == (expected & 1) &&
            completion == ((expected >> 1) & 1) && retry_only == UINT64_C(0));
    }
  });
}
