// Compares speculative atomic qualification on the two translation specializations.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "test.hpp"

inline void check_speculative_atomics(std::uint64_t addr, unsigned expected_outcome) {
  address = addr;
  for (unsigned op : {3u, 4u, 5u}) { // LR.D, SC.D, AMOSWAP.D
    access = op;
    ex_valid = 1;
    eval();
    CHECK(index_valid && index_address == addr && !result_valid && !physical_valid);
    tick_model();
    ex_valid = 0;
    eval();
    CHECK(result_valid && outcome == expected_outcome && !resolve_valid &&
          !physical_valid && !wb_fault && !lookup_guest.ppage_ufault);
    tick_model();
    eval();
    CHECK(!result_valid && !physical_valid && memory_drained);
  }
}
