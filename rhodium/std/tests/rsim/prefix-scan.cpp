// Checks every prefix against serial arithmetic and first-valid selection oracles.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <cstdint>

int main() {
  return run_test([] {
    // Odd length crosses multiple doubling boundaries. Exercise every possible
    // validity pattern, including holes, all invalid, and competing candidates.
    for (unsigned pattern = 0; pattern < 512; ++pattern) {
      for (unsigned seed = 0; seed < 16; ++seed) {
        for (unsigned lane = 0; lane < 9; ++lane) {
          operands[lane] = (seed * 53 + lane * 37 + pattern) & 255;
          candidates[lane].pvalid = (pattern >> lane) & 1;
          candidates[lane].pdata = (seed * 29 + lane * 17) & 255;
        }
        eval();
        unsigned sum = 0, chosen = 0;
        bool found = false;
        for (unsigned lane = 0; lane < 9; ++lane) {
          sum = (sum + operands[lane]) & 255;
          if (!found && candidates[lane].pvalid) {
            chosen = candidates[lane].pdata;
            found = true;
          }
          CHECK(sums[lane] == sum);
          CHECK(selected[lane].pvalid == found);
          if (found)
            CHECK(selected[lane].pdata == chosen);
        }
        CHECK(total == sum);
        CHECK(singleton == operands[0]);
      }
    }
  });
}
