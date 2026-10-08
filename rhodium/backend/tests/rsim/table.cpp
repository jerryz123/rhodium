// Checks table behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    for (int i = 0; i < 256; i++) {
      addr = slice(i, 7, 0);
      eval();
      CHECK(out == slice(i, 7, 0));
    }
  });
}
