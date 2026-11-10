// Checks low and overflowing guest prefetch addresses with both translation stages Bare.
// SPDX-License-Identifier: Apache-2.0
#pragma once

#include "test.hpp"
#include <cstdint>

template <typename CheckDemand> void check_guest_bare_prefetch(CheckDemand check_demand) {
  constexpr std::uint64_t line = 0x2500;
  for (std::uint64_t address : {line, line | (UINT64_C(1) << 56)}) {
    prefetch_in = {1, {address, 2}}; // Read hint.
    eval();
    CHECK(!physical_prefetch_out.pvalid);
    check_demand();
    tick_model();
    prefetch_in = {};
    eval();
    CHECK(!physical_prefetch_out.pvalid);
    check_demand();
    tick_model();
    eval();
    CHECK(physical_prefetch_out.pvalid == (address == line));
    if (physical_prefetch_out.pvalid)
      CHECK(physical_prefetch_out.pbits.paddress == line && physical_prefetch_out.pbits.poperation == 2);
    check_demand();
    tick_model();
    eval();
    CHECK(!physical_prefetch_out.pvalid);
    check_demand();
  }
}
