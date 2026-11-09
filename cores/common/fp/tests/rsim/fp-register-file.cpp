// Verifies two-write forwarding, retention, all three reads, and writable f0.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
auto &first_write = writes_0_in;
auto &second_write = writes_1_in;

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    reset = UINT64_C(1);
    read_address_1 = UINT64_C(0);
    read_address_2 = UINT64_C(5);
    read_address_3 = UINT64_C(6);
    first_write = {};
    second_write = {};
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    reset = UINT64_C(0);

    first_write.pvalid = UINT64_C(1);
    first_write.pbits.paddress = UINT64_C(0);
    first_write.pbits.pdata = UINT64_C(81985529216486895);
    second_write.pvalid = UINT64_C(1);
    second_write.pbits.paddress = UINT64_C(6);
    second_write.pbits.pdata = UINT64_C(18364758544493064720);
    eval();
    CHECK(read_data_1 == UINT64_C(81985529216486895));
    CHECK(read_data_3 == UINT64_C(18364758544493064720));
    tick_model();
    first_write.pvalid = UINT64_C(0);
    second_write.pvalid = UINT64_C(0);
    CHECK(read_data_1 == UINT64_C(81985529216486895));
    CHECK(read_data_3 == UINT64_C(18364758544493064720));

    first_write.pvalid = UINT64_C(1);
    first_write.pbits.paddress = UINT64_C(5);
    first_write.pbits.pdata = UINT64_C(1229801703532086340);
    eval();
    CHECK(read_data_2 == UINT64_C(1229801703532086340));
    tick_model();
    first_write.pvalid = UINT64_C(0);
    CHECK(read_data_2 == UINT64_C(1229801703532086340));

    read_address_1 = UINT64_C(5);
    read_address_3 = UINT64_C(5);
    first_write.pvalid = UINT64_C(1);
    first_write.pbits.pdata = UINT64_C(6148933456521300104);
    second_write.pvalid = UINT64_C(1);
    second_write.pbits.paddress = UINT64_C(5);
    second_write.pbits.pdata = UINT64_C(11068065209510513868);
    eval();
    CHECK(read_data_1 == second_write.pbits.pdata);
    CHECK(read_data_2 == second_write.pbits.pdata);
    CHECK(read_data_3 == second_write.pbits.pdata);
    tick_model();
    first_write.pvalid = UINT64_C(0);
    second_write.pvalid = UINT64_C(0);
    CHECK(read_data_1 == UINT64_C(11068065209510513868));
    CHECK(read_data_2 == UINT64_C(11068065209510513868));
    CHECK(read_data_3 == UINT64_C(11068065209510513868));
  });
}
