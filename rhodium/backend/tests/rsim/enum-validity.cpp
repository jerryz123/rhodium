// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    for (unsigned s = 0; s < 8; ++s) {
      for (unsigned d = 0; d < 4; ++d) {
        for (unsigned h = 0; h < 4; ++h) {
          sparse = s;
          dense = d;
          one_hot = h;
          eval();
          CHECK(sparse_valid == (s == 0 || s == 3 || s == 7));
          CHECK(dense_valid);
          CHECK(one_hot_valid == (h == 1 || h == 2));
        }
      }
    }
  });
}
