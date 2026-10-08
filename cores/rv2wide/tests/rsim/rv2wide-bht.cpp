// Checks frontend prediction, retained packet ownership, and precise recovery timing.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
using Instruction =
    std::remove_cvref_t<decltype(instructions_out.pbits.pentries[0])>;
using Packet = std::remove_cvref_t<decltype(instructions_out.pbits)>;
int cycles = 0, scenario = 0, source_cycle = -1, target_cycle = -1,
    branch_cycle = -1, btb_cycle = -1, checks = 0, history_followups = 0,
    recovery_packets = 0;
bool response_valid = 0, response_replay = 0, taken = 1, saw_replay = 0,
     replay_once = 0;
std::uint64_t response_data = 0, branch_pc = 0, target_pc = 0;
std::uint16_t expected_index = 0, expected_history = 0;
void falling_update() {}
void drive() {
  translation_in = {translation_out.prequest.pbits, {0, 0, 0, {}}, 0};
  memory_in = {
      {std::uint8_t(response_valid),
       {response_data, std::uint8_t(false), std::uint8_t(response_replay)}}};
}
std::uint64_t word(std::uint64_t address) {
  if (scenario == 11 || scenario == 12)
    return (UINT64_C(0x00100013) << 32) |
           (address == 0x110 ? 0x20000063 : 0x00100013);
  if (scenario == 9)
    return (UINT64_C(0x00100013) << 32) |
           (address == 0x108 ? 0x20000063 : 0x00100013);
  if (address == 0x100)
    switch (scenario) {
    case 2: {
      return (field(UINT64_C(0x20000063), 32, 32) | field(UINT64_C(1), 16, 16) |
              field(UINT64_C(1), 16, 0));
    } break;
    case 3:
    case 4:
    case 10: {
      return (field(UINT64_C(0x63), 16, 48) |
              field(UINT64_C(0x100010001), 48, 0));
    } break;
    case 5: {
      return (field(UINT64_C(0x10001), 32, 32) |
              field(UINT64_C(0xc001), 16, 16) | field(UINT64_C(0xc001), 16, 0));
    } break;
    default: {
      return (field(UINT64_C(0x100013), 32, 32) |
              field(UINT64_C(0x20000063), 32, 0));
    } break;
    }
  if (address == 0x108 && (scenario == 3 || scenario == 4 || scenario == 10))
    return UINT64_C(0x0001000100012000);
  return UINT64_C(0x0010001300100013);
}

void observe() {
  if (reset) {
    cycles = 0;
    defer(response_valid, 0);
    defer(response_replay, 0);
  } else {
    cycles++;
    if (cycles > 150)
      fail(1, "BHT frontend timeout scenario=%0d", scenario);
    defer(response_valid, memory_out.prequest.pvalid && !memory_out.ps1_ukill &&
                              !memory_out.pflush);
    defer(response_data, word(memory_out.prequest.pbits.paddress));
    defer(response_replay,
          replay_once && !saw_replay &&
              memory_out.prequest.pbits.paddress == UINT64_C(0x108));
    if (replay_once && memory_out.prequest.pvalid &&
        memory_out.prequest.pbits.paddress == UINT64_C(0x108) &&
        !memory_out.ps1_ukill)
      saw_replay = 1;
    if (virtual_lookup_out.pvalid && virtual_lookup_in.pready) {
      if (virtual_lookup_out.pbits == (branch_pc & ~UINT64_C(7)) &&
          source_cycle < 0)
        source_cycle = cycles;
      if (source_cycle >= 0 && cycles > source_cycle &&
          virtual_lookup_out.pbits == (target_pc & ~UINT64_C(7)) &&
          target_cycle < 0)
        target_cycle = cycles;
      if (scenario == 1 && cycles == source_cycle + 1 &&
          virtual_lookup_out.pbits == UINT64_C(0x300))
        btb_cycle = cycles;
    }
    if (instructions_out.pvalid && instructions_in.pready)
      for (int lane = 0; lane < int(instructions_out.pbits.pcount); lane++) {
        Instruction insn;
        insn = instructions_out.pbits.pentries[lane];
        if (insn.ppc == branch_pc && branch_cycle < 0) {
          branch_cycle = cycles;
          CHECK(insn.pdirection.pvalid &&
                insn.pdirection.pindex == expected_index &&
                insn.pdirection.phistory == expected_history &&
                insn.pdirection.ptaken == taken);
          CHECK(insn.pprediction.pvalid == taken &&
                (!taken || insn.pprediction.ptarget == target_pc));
          if (taken)
            CHECK(int(instructions_out.pbits.pcount) == lane + 1);
        }
        if (taken && scenario != 5 && insn.ppc == target_pc) {
          std::uint16_t after_history;
          after_history = (field(bits(expected_history, 8, 0), 9, 1) |
                           field(UINT64_C(1), 1, 0));
          CHECK(insn.pdirection.phistory == after_history &&
                insn.pdirection.pindex ==
                    (((((target_pc >> 3) ^ ((after_history)&low_mask(64))) &
                       1023) *
                          4 +
                      ((target_pc >> 1) & 3)) &
                     low_mask(12)));
          history_followups++;
        }
        if (scenario == 8 && insn.ppc == UINT64_C(0x600)) {
          CHECK(insn.pdirection.phistory == UINT64_C(7) &&
                insn.pdirection.pindex == UINT64_C(0x31c));
          recovery_packets++;
        }
      }
  }
}
void begin_case(int which, std::uint8_t want_taken = 1,
                std::uint8_t btb_taken = 0, int history = 0) {
  falling();
  reset = 1;
  start_in = {};
  redirect_in = {};
  flush_in = {};
  branch_update_in = {};
  direction_update_in = {};
  history_restore_in = {};
  history_commit_in = {};
  scenario = which;
  taken = want_taken;
  expected_history = std::uint16_t(history & 1023);
  virtual_lookup_in.pready = 0;
  instructions_in.pready = 1;
  source_cycle = -1;
  target_cycle = -1;
  branch_cycle = -1;
  btb_cycle = -1;
  saw_replay = 0;
  replay_once = which == 4;
  branch_pc =
      (which == 11 || which == 12)
          ? UINT64_C(0x110)
          : (which == 9
                 ? UINT64_C(0x108)
                 : (which == 2 ? UINT64_C(0x104)
                               : ((which == 3 || which == 4 || which == 10)
                                      ? UINT64_C(0x106)
                                      : (which == 5 ? UINT64_C(0x102)
                                                    : UINT64_C(0x100)))));
  target_pc = want_taken
                  ? (which == 5 ? branch_pc : branch_pc + UINT64_C(0x200))
                  : ((branch_pc & ~UINT64_C(7)) + 8);
  expected_index =
      (((((branch_pc >> 3) ^ ((history)&low_mask(64))) & 1023) * 4 +
        ((branch_pc >> 1) & 3)) &
       low_mask(12));
  for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
    falling();
  reset = 0;
  start_in = {UINT64_C(1), UINT64_C(0x100)};
  falling();
  start_in = {};
  if (btb_taken) {
    branch_update_in = {UINT64_C(1),
                        {branch_pc, branch_pc + UINT64_C(0x200), UINT64_C(1),
                         UINT64_C(1), UINT64_C(1), which == 10, UINT64_C(0),
                         UINT64_C(0), branch_pc + 4}};
    falling();
    branch_update_in = {};
  }
  if (want_taken) {
    direction_update_in = {UINT64_C(1), expected_index, UINT64_C(1)};
    for (int repeat_index = 0; repeat_index < (2); ++repeat_index)
      falling();
    direction_update_in = {};
  }
  history_restore_in = {UINT64_C(1), std::uint16_t(history & 1023)};
  falling();
  history_restore_in = {};
}
void finish_case() {
  until([&] { return branch_cycle >= 0; });
  for (int repeat_index = 0; repeat_index < (4); ++repeat_index)
    falling();
  CHECK(target_cycle >= 0);
  checks++;
}
int main() {
  return run_test([] {
    reset = 1;
    instructions_in.pready = 1;
    virtual_lookup_in.pready = 1;
    // Miss -> taken redirects at fresh S2, even with the entire assembler stalled.
    begin_case(0);
    instructions_in.pready = 0;
    virtual_lookup_in.pready = 1;
    until([&] { return target_cycle >= 0; });
    falling();
    CHECK(target_cycle == source_cycle + 2 && branch_cycle < 0);
    for (int repeat_index = 0; repeat_index < (6); ++repeat_index)
      falling();
    instructions_in.pready = 1;
    finish_case();
    // The third response exhausts block storage behind two stalled older blocks.
    // Both correction directions must save their target without overbooking it.
    for (int which = 11; which <= 12; which++) {
      begin_case(which, which == 11, which == 12);
      instructions_in.pready = 0;
      virtual_lookup_in.pready = 1;
      until([&] { return source_cycle >= 0; });
      for (int repeat_index = 0; repeat_index < (9); ++repeat_index)
        falling();
      CHECK(target_cycle < 0 && !virtual_lookup_out.pvalid);
      instructions_in.pready = 1;
      finish_case();
    }
    // An older block occupies the assembler head while the branch's fresh S2
    // outcome redirects. Predicting the queue head instead would miss this edge.
    begin_case(9);
    instructions_in.pready = 0;
    virtual_lookup_in.pready = 1;
    until([&] { return target_cycle >= 0; });
    falling();
    CHECK(target_cycle == source_cycle + 2 && branch_cycle < 0);
    for (int repeat_index = 0; repeat_index < (6); ++repeat_index)
      falling();
    instructions_in.pready = 1;
    finish_case();
    // A taken S1 BTB direction is overridden to not-taken, without losing the branch.
    begin_case(1, 0, 1);
    virtual_lookup_in.pready = 1;
    finish_case();
    CHECK(target_cycle == source_cycle + 2 && btb_cycle == source_cycle + 1);
    // A direction-only override must retain the valid BTB target. Refetch
    // without clearing predictors and observe that same provisional S1 offer.
    source_cycle = -1;
    target_cycle = -1;
    branch_cycle = -1;
    btb_cycle = -1;
    redirect_in = {UINT64_C(1),
                   {UINT64_C(0x100),
                    UINT64_C(0x100),
                    {UINT64_C(0), UINT64_C(0), UINT64_C(0), {}}}};
    history_restore_in = {UINT64_C(1), UINT64_C(0)};
    falling();
    redirect_in = {};
    history_restore_in = {};
    finish_case();
    CHECK(btb_cycle == source_cycle + 1);
    begin_case(2);
    virtual_lookup_in.pready = 1;
    finish_case();
    CHECK(target_cycle == source_cycle + 2);
    begin_case(3);
    virtual_lookup_in.pready = 1;
    finish_case();
    CHECK(target_cycle == source_cycle + 3);
    begin_case(4);
    virtual_lookup_in.pready = 1;
    finish_case();
    CHECK(saw_replay);
    // The BTB mistakes the byte-six prefix for a complete compressed branch.
    // S2 must fetch its continuation and retain the original BHT row.
    begin_case(10, 1, 1);
    virtual_lookup_in.pready = 1;
    finish_case();
    // Both compressed branches share a row; only the second bank predicts taken.
    begin_case(5);
    virtual_lookup_in.pready = 1;
    finish_case();
    begin_case(6, 1, 0, 5);
    virtual_lookup_in.pready = 1;
    finish_case();
    // Hold the S2 replacement at array admission, then verify the saved offer.
    begin_case(7);
    virtual_lookup_in.pready = 1;
    until([&] { return source_cycle >= 0; });
    falling();
    virtual_lookup_in.pready = 0;
    for (int repeat_index = 0; repeat_index < (5); ++repeat_index) {
      falling();
      CHECK(virtual_lookup_out.pvalid &&
            virtual_lookup_out.pbits == UINT64_C(0x300));
    }
    virtual_lookup_in.pready = 1;
    finish_case();
    // Architectural recovery outranks a simultaneous direction correction.
    begin_case(8);
    virtual_lookup_in.pready = 1;
    until([&] { return memory_in.presponse.pvalid; });
    falling();
    redirect_in = {UINT64_C(1),
                   {UINT64_C(0x100),
                    UINT64_C(0x600),
                    {UINT64_C(0), UINT64_C(0), UINT64_C(0), {}}}};
    history_restore_in = {UINT64_C(1), UINT64_C(7)};
    settle();
    CHECK(virtual_lookup_out.pvalid &&
          virtual_lookup_out.pbits == UINT64_C(0x600) &&
          !instructions_out.pvalid);
    falling();
    redirect_in = {};
    history_restore_in = {};
    until([&] { return recovery_packets > 0; });
    falling();
    checks++;
    // Commit two outcomes without table writes, then recover through committed history.
    // Older taken then younger not-taken folds 00 -> 01 -> 10, not 01 or 00.
    begin_case(0, 0);
    expected_history = 2;
    expected_index = ((((UINT64_C(0x100) >> 3) ^ 2) * 4) & low_mask(12));
    history_commit_in = {1, {{1, 1}, {1, 0}}};
    falling();
    history_commit_in = {};
    redirect_in = {UINT64_C(1),
                   {UINT64_C(0x100),
                    UINT64_C(0x100),
                    {UINT64_C(0), UINT64_C(0), UINT64_C(0), {}}}};
    falling();
    redirect_in = {};
    virtual_lookup_in.pready = 1;
    finish_case();
    CHECK(history_followups >= 5);
  });
}