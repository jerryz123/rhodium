// Checks banked saturation, current-state training, and lazy clear/reset.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

std::array<std::array<unsigned, 4>, 8> expected;
void cold_model() {
  for (auto &banks : expected)
    banks.fill(1);
}
void check_rows() {
  for (unsigned r = 0; r < 8; ++r) {
    row = r;
    prefix_row = (r + 3) % 8;
    eval();
    for (unsigned b = 0; b < 4; ++b)
      CHECK(counters[b] == expected[r][b]);
    CHECK(prefix_counter == expected[(r + 3) % 8][3]);
  }
}
int main() {
  return run_test([] {
    reset = 1;
    clear_in = {};
    update_in = {};
    cold_model();
    for (unsigned i = 0; i < 3; ++i)
      tick_model();
    reset = 0;
    check_rows();
    for (unsigned pass = 0; pass < 10; ++pass)
      for (unsigned r = 0; r < 8; ++r)
        for (unsigned b = 0; b < 4; ++b) {
          tick_model();
          update_in = {1, {std::uint8_t(r * 4 + b), pass < 5}};
          tick_model();
          update_in = {};
          auto &counter = expected[r][b];
          counter = pass < 5 ? std::min(counter + 1, 3u)
                             : (counter ? counter - 1 : 0);
          check_rows();
        }
    // Adjacent writes must use the current counter at the saved index.
    tick_model();
    update_in = {1, {7, 1}};
    tick_model();
    tick_model();
    update_in = {};
    expected[1][3] = 2;
    check_rows();
    tick_model();
    clear_in.pvalid = 1;
    update_in = {1, {7, 1}};
    cold_model();
    check_rows();
    tick_model();
    clear_in = {};
    update_in = {};
    check_rows();
    tick_model();
    update_in = {1, {7, 1}};
    tick_model();
    update_in = {};
    reset = 1;
    tick_model();
    reset = 0;
    check_rows();
  });
}
