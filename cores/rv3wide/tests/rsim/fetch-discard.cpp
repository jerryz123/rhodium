// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

static constexpr std::uint64_t old_pc = 0x1ff8;
static constexpr std::uint64_t legal_pc = 0x40003000;
static constexpr std::uint64_t boundary_pc = (UINT64_C(1) << 38) - 16;
static constexpr std::uint64_t nops = UINT64_C(0x0000001300000013);
static constexpr std::uint64_t jump_block = UINT64_C(0x00000013ff1ff06f); // jal x0,-16; nop

static void prime_itlb() {
  dut.inputs = {};
  reset = 1;
  eval(); tick_model(); tick_model();
  reset = 0;
  cache_word = nops;
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

static void issue_at(std::uint64_t pc, std::uint32_t instruction) {
  auto &entry = instructions_in.pbits.pentries[0];
  entry.ppc = pc;
  entry.psequential_upc = pc + 4;
  entry.pinstruction = entry.praw_uinstruction = instruction;
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
}

static void start_old_fetch() {
  fetch_start_in = {1, old_pc};
  eval(); CHECK(array_lookup_out.pvalid && array_lookup_out.pbits == old_pc);
  tick_model(); fetch_start_in = {}; instructions_in.pvalid = 0;
  eval(); CHECK(s1_live && s1_address == old_pc && !s1_miss);
  CHECK(array_lookup_out.pvalid && array_lookup_out.pbits == old_pc + 8);
  tick_model();
}

static void collision(bool invalid, bool stall_restart, bool retry_on_edge) {
  prime_itlb();
  const auto branch_pc = invalid ? boundary_pc : legal_pc;
  issue_at(branch_pc, 0x040008ef); // jal x17,+64
  start_old_fetch();
  cache_word = jump_block; // a live copy of the discarded bytes would redirect to old_pc - 16
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
  physical_replay = 0;
  eval();
  CHECK(!pte_out.prequest.pvalid); // a walker admitted on the collision edge would be issuing now
  CHECK(retired_0_out.pvalid && retired_0_out.pbits.prd == 17 && retired_0_out.pbits.pdata == branch_pc + 4);
  if (invalid) {
    CHECK(!s1_live && !array_lookup_out.pvalid);
    array_lookup_in.pready = 1;
    tick_model(); eval();
    CHECK(redirect_out.pvalid && redirect_out.pbits.ptarget == 0); // precise target fault to mtvec
  } else {
    if (stall_restart) {
      for (unsigned hold = 0; hold < 3; ++hold) {
        CHECK(!s1_live && array_lookup_out.pvalid && array_lookup_out.pbits == branch_pc + 64);
        CHECK(!pte_out.prequest.pvalid);
        tick_model(); eval();
      }
      array_lookup_in.pready = 1;
      tick_model(); eval();
    }
    CHECK(s1_live && s1_address == branch_pc + 64 && s1_miss && !s1_discard);
    CHECK(array_lookup_out.pvalid && array_lookup_out.pbits == ((branch_pc + 64) & ~UINT64_C(7)) + 8);
    tick_model(); eval();
    CHECK(pte_out.prequest.pvalid && pte_out.prequest.pbits.paddress == 0x3008);
  }
}

static void unfenced_jump() {
  prime_itlb();
  array_lookup_in.pready = 1;
  start_old_fetch();
  cache_word = jump_block;
  eval();
  CHECK(s2_physical && s1_live && s1_address == old_pc + 8 && s1_miss);
  CHECK(fetch_packet && fetched_instruction == std::uint32_t(jump_block));
  CHECK(s1_discard && array_lookup_out.pvalid && array_lookup_out.pbits == old_pc - 16);
  tick_model(); eval();
  CHECK(!pte_out.prequest.pvalid && s1_live && s1_address == old_pc - 16);
}

static void fence_collision(bool stall_restart) {
  prime_itlb();
  issue_at(old_pc - 4, 0x0000100f); // fence.i restarts at the very same S2 PC
  tick_model(); // the fence invalidates in WB, one stage after a MEM redirect
  start_old_fetch();
  cache_word = jump_block;
  array_lookup_in.pready = !stall_restart;
  eval();
  CHECK(instruction_invalidate_out.pvalid && redirect_out.pvalid && redirect_out.pbits.ptarget == old_pc);
  CHECK(s2_physical && !physical_replay && s1_live && s1_address == old_pc + 8 && s1_miss);
  CHECK(s1_discard && !fetch_packet && !pte_out.prequest.pvalid);
  CHECK(array_lookup_out.pvalid && array_lookup_out.pbits == old_pc);
  tick_model();

  cache_word = nops; // replacement code, no jump to discover or execute
  array_lookup_in.pready = 1;
  eval();
  CHECK(!pte_out.prequest.pvalid && !fetch_packet && !instruction_invalidate_out.pvalid);
  if (stall_restart) {
    CHECK(!s1_live && array_lookup_out.pvalid && array_lookup_out.pbits == old_pc);
    tick_model(); eval();
  }
  CHECK(s1_live && s1_address == old_pc && !s1_miss);
  CHECK(array_lookup_out.pvalid && array_lookup_out.pbits == old_pc + 8);
  CHECK(!pte_out.prequest.pvalid && !fetch_packet);
  tick_model(); eval();
  CHECK(s2_physical && s1_live && s1_miss && s1_address == old_pc + 8 && !s1_discard);
  CHECK(!pte_out.prequest.pvalid && fetch_packet && fetched_instruction == 0x13);
  tick_model(); eval();
  CHECK(pte_out.prequest.pvalid && pte_out.prequest.pbits.paddress == 0x3000);
}

int main() {
  return run_test([] {
    for (bool invalid : {false, true})
      for (bool stall : {false, true})
        for (bool retry : {true, false}) collision(invalid, stall, retry);
    unfenced_jump();
    for (bool stall : {false, true}) fence_collision(stall);
  });
}
