// Checks simultaneous writes, write-first reads, retention, and x0 behavior.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    reset = 1;
    read_address_1 = 5;
    read_address_2 = 6;
    writes_0_in = {};
    writes_1_in = {};
    tick_model();
    tick_model();
    reset = 0;
    writes_0_in = {1, {5, 11}};
    writes_1_in = {1, {6, 22}};
    eval();
    CHECK(read_data_1 == 11 && read_data_2 == 22);
    tick_model();
    writes_0_in.pvalid = writes_1_in.pvalid = 0;
    eval();
    CHECK(read_data_1 == 11 && read_data_2 == 22);
    read_address_1 = 0;
    read_address_2 = 7;
    writes_0_in = {1, {0, UINT64_MAX}};
    writes_1_in = {1, {7, 33}};
    eval();
    CHECK(read_data_1 == 0 && read_data_2 == 33);
    tick_model();
    writes_0_in.pvalid = writes_1_in.pvalid = 0;
    eval();
    CHECK(read_data_1 == 0 && read_data_2 == 33);
  });
}
