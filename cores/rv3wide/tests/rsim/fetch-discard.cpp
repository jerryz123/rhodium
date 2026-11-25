// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

static constexpr std::uint64_t old_pc = 0x1ff8;
static constexpr std::uint64_t legal_pc = 0x40003000;
static constexpr std::uint64_t boundary_pc = (UINT64_C(1) << 38) - 16;

static void prime_itlb() {
  dut.inputs = {};
  reset = 1;
  eval(); tick_model(); tick_model();
  reset = 0;
  memory_in.pdrained = 1;
  pte_in.prequest.pready = 1;
  prime = 1;
  // Map the page containing 0x1ff8, leaving 0x2000 and the restart page cold.
  const unsigned addresses[] = {0x3000, 0x4000, 0x5008};
  const unsigned ptes[] = {(4 << 10) | 1, (5 << 10) | 1, 0xcb};
  unsigned reads = 0;
  for (unsigned cycles = 0;; ++cycles) {
    eval(); CHECK(cycles < 60);
    if (!s1_miss) break;
    const bool read = pte_out.prequest.pvalid;
    if (read) { CHECK(reads < 3); CHECK(pte_out.prequest.pbits.paddress == addresses[reads]); }
    tick_model();
    pte_in.presponse.pvalid = read;
    if (read) pte_in.presponse.pbits = ptes[reads++];
  }
  CHECK(reads == 3);
  prime = 0;
  pte_in.presponse = {};
}

static void collision(bool invalid, bool stall_restart, bool retry_on_edge) {
  prime_itlb();
  const auto branch_pc = invalid ? boundary_pc : legal_pc;
  auto &entry = instructions_in.pbits.pentries[0];
  entry.ppc = branch_pc;
  entry.psequential_upc = branch_pc + 4;
  entry.pinstruction = entry.praw_uinstruction = 0x040008ef; // jal x17,+64
  instructions_in.pbits.pcount = 1;
  instructions_in.pvalid = 1;
  array_lookup_in.pready = 1;
  for (unsigned cycles = 0;; ++cycles) {
    eval(); CHECK(cycles < 20);
    if (issued) break;
    const bool accepted = instructions_in.pvalid && instructions_out.pready;
    tick_model();
    if (accepted) instructions_in.pvalid = 0;
  }
  fetch_start_in = {1, old_pc};
  eval(); CHECK(array_lookup_out.pvalid && array_lookup_out.pbits == old_pc);
  tick_model(); fetch_start_in = {}; instructions_in.pvalid = 0;
  eval(); CHECK(s1_live && s1_address == old_pc && !s1_miss);
  CHECK(array_lookup_out.pvalid && array_lookup_out.pbits == old_pc + 8);
  tick_model();
  array_lookup_in.pready = !stall_restart;
  // Toggle the physical outcome without advancing either token. Both the
  // hit and replay must lose to discard; neither may launch the old walk.
  for (bool retry : {!retry_on_edge, retry_on_edge}) {
    physical_replay = retry;
    eval();
    CHECK(memory_stage_0_out.pvalid && memory_stage_0_out.pbits.ppc == branch_pc);
    CHECK(s2_physical && s1_live && s1_address == old_pc + 8 && s1_miss);
    CHECK(fetch_flush_out.pvalid && s1_discard && !fetch_packet);
    CHECK(bool(redirect_out.pvalid) == !invalid);
    CHECK(bool(array_lookup_out.pvalid) == !invalid);
    if (!invalid) CHECK(array_lookup_out.pbits == branch_pc + 64);
    CHECK(!pte_out.prequest.pvalid);
  }
  tick_model();
  array_lookup_in.pready = 1; physical_replay = 0;
  eval();
  CHECK(!pte_out.prequest.pvalid); // a walker admitted on the collision edge would be issuing now
  CHECK(retired_0_out.pvalid && retired_0_out.pbits.prd == 17 && retired_0_out.pbits.pdata == branch_pc + 4);
  if (invalid) {
    CHECK(!s1_live && !array_lookup_out.pvalid);
    tick_model(); eval();
    CHECK(redirect_out.pvalid && redirect_out.pbits.ptarget == 0); // precise target fault to mtvec
  } else {
    if (stall_restart) {
      CHECK(!s1_live && array_lookup_out.pvalid && array_lookup_out.pbits == branch_pc + 64);
      tick_model(); eval();
    }
    CHECK(s1_live && s1_address == branch_pc + 64 && s1_miss && !s1_discard);
    tick_model(); eval();
    CHECK(pte_out.prequest.pvalid && pte_out.prequest.pbits.paddress == 0x3008);
  }
}

int main() {
  return run_test([] {
    for (bool invalid : {false, true})
      for (bool stall : {false, true})
        for (bool retry : {true, false}) collision(invalid, stall, retry);
  });
}
