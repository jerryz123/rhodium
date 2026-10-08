// Checks output-greedy matching, independent rotation, and all request images.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void check_grants(unsigned a, unsigned b, unsigned c) {
  CHECK(pack_bits(grants[0]) == a && pack_bits(grants[1]) == b &&
        pack_bits(grants[2]) == c);
}
int main() {
  return run_test([] {
    reset = 1;
    requests = {};
    accepts = {};
    tick_model();
    reset = 0;
    requests = {{{1, 1}, {1, 1}, {1, 1}}};
    accepts = {1, 1};
    eval();
    check_grants(1, 2, 0);
    tick_model();
    check_grants(0, 1, 2);
    tick_model();
    check_grants(2, 0, 1);
    tick_model();
    accepts = {0, 1};
    eval();
    check_grants(1, 2, 0);
    tick_model();
    check_grants(1, 0, 2);
    requests = {{{1, 1}, {0, 0}, {0, 0}}};
    accepts = {1, 1};
    eval();
    check_grants(1, 0, 0);
    tick_model();
    check_grants(1, 0, 0);
    accepts = {};
    for (unsigned image = 0; image < 64; ++image) {
      for (unsigned i = 0; i < 3; ++i)
        for (unsigned j = 0; j < 2; ++j)
          requests[i][j] = (image >> (2 * i + j)) & 1;
      eval();
      for (unsigned i = 0; i < 3; ++i) {
        CHECK((pack_bits(grants[i]) & ~pack_bits(requests[i])) == 0);
        CHECK((pack_bits(grants[i]) & (pack_bits(grants[i]) - 1)) == 0);
      }
      for (unsigned output = 0; output < 2; ++output) {
        unsigned granted = 0;
        for (unsigned i = 0; i < 3; ++i)
          granted += grants[i][output];
        CHECK(granted <= 1);
      }
    }
  });
}
