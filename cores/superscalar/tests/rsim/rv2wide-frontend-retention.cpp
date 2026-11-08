// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "fetch-fault.hpp"

std::uint64_t prefix_pc = 0, target_pc = 0, fault_pc = 0;
bool inject_fault = false, replay_once = false;
bool response_valid = false, response_replay = false;
std::uint64_t response_data = 0;
unsigned continuation_requests = 0, target_requests = 0;

std::uint64_t physical(std::uint64_t pc) {
  if ((pc & ~UINT64_C(7)) == (prefix_pc & ~UINT64_C(7))) return 0x100;
  if ((pc & ~UINT64_C(7)) == std::uint64_t(prefix_pc + 2)) return 0x108;
  return 0x300;
}

void settle() {
  memory_in = {{std::uint8_t(response_valid), {response_data, 0, std::uint8_t(response_replay)}}};
  for (unsigned pass = 0; pass < 2; ++pass) {
    eval();
    const auto pc = translation_out.prequest.pbits;
    translation_in = {physical(pc), {0, 0, 0, {}}, 0};
    if (inject_fault && pc == fault_pc)
      translation_in.presolution = {1, 12, pc, {}}; // Instruction page fault.
  }
  eval();
}

void cycle() {
  settle();
  if (!reset && virtual_lookup_out.pvalid && virtual_lookup_in.pready &&
      virtual_lookup_out.pbits == (target_pc & ~UINT64_C(7)))
    ++target_requests;
  const bool request = !reset && memory_out.prequest.pvalid &&
                       !memory_out.ps1_ukill && !memory_out.pflush;
  const auto address = memory_out.prequest.pbits.paddress;
  bool next_replay = false;
  if (request && address == 0x108) {
    ++continuation_requests;
    next_replay = replay_once;
    replay_once = false;
  }
  const auto data = address == 0x100 ? UINT64_C(0x0067000100010001) :
                    address == 0x108 ? UINT64_C(0x0001000100010001) :
                    UINT64_C(0x0010001300100013);
  tick_model();
  response_valid = request;
  response_replay = next_replay;
  response_data = data;
  settle();
}

void initialize(std::uint64_t pc, std::uint64_t target, bool replay, bool fault) {
  prefix_pc = pc; target_pc = target; fault_pc = pc + 2;
  inject_fault = fault; replay_once = replay;
  response_valid = false; response_replay = false;
  continuation_requests = 0; target_requests = 0;
  reset = 1;
  start_in = {}; redirect_in = {}; flush_in = {}; invalidate_in = {};
  branch_update_in = {}; predictor_restore_in = {}; predictor_clear_in = {};
  direction_update_in = {}; ras_resolution_in = {};
  history_commit_in = {}; history_restore_in = {};
  instructions_in.pready = 0; virtual_lookup_in.pready = 0;
  cycle(); cycle(); reset = 0;
  start_in = {1, pc}; cycle(); start_in = {};
  // Start clears predictors. Train while the original fetch cursor is blocked.
  branch_update_in = {1, {pc, target, 1, 0, 1, 0, 0, 0, pc + 4}};
  cycle(); branch_update_in = {};
}

void run_prefix(std::uint64_t pc, std::uint64_t target, bool replay, bool fault) {
  initialize(pc, target, replay, fault);
  virtual_lookup_in.pready = 1;
  settle();
  CHECK(virtual_lookup_out.pvalid && virtual_lookup_out.pbits == (pc & ~UINT64_C(7)));
  cycle();
  virtual_lookup_in.pready = 0;
  // S1 drains into S2 and the saved cursor while the continuation cannot issue.
  for (unsigned i = 0; i < 3; ++i) cycle();
  branch_update_in = {1, {pc, target ^ 0x100, 1, 0, 1, 0, 0, 0, pc + 4}};
  cycle(); branch_update_in = {};
  for (unsigned i = 0; i < 4; ++i) {
    settle();
    CHECK(virtual_lookup_out.pvalid && virtual_lookup_out.pbits == std::uint64_t(pc + 2));
    CHECK(!instructions_out.pvalid);
    cycle();
  }
  virtual_lookup_in.pready = 1;
  unsigned waited = 0;
  while (!instructions_out.pvalid && waited++ < 80) cycle();
  CHECK(instructions_out.pvalid);
  for (unsigned i = 0; i < 5; ++i) {
    const auto instruction = instructions_out.pbits.pentries[0];
    CHECK(instructions_out.pbits.pcount == 1 && instruction.ppc == pc);
    if (fault) {
      CHECK(instruction.pfault.pvalid && instruction.pfault.pcause == 12 && parcel_fault_address(instruction) == std::uint64_t(pc + 2));
      CHECK(!instruction.pprediction.pvalid);
    } else {
      CHECK(instruction.pfault.pvalid == 0 && instruction.praw_uinstruction == 0x00010067);
      CHECK(instruction.pprediction.pvalid && instruction.pprediction.ppc == pc &&
            instruction.pprediction.ptarget == target && !instruction.pprediction.pcompressed &&
            instruction.pprediction.pras_uaction == 0);
      CHECK(instruction.psequential_upc == std::uint64_t(pc + 4));
    }
    cycle();
  }
  if (replay) CHECK(continuation_requests >= 2);
  if (!fault) CHECK(target_requests > 0);
  instructions_in.pready = 1;
  cycle();
}

int main() {
  return run_test([] {
    for (auto pc : {UINT64_C(0x1ffe), UINT64_C(0xffffffc000000ffe), UINT64_MAX - 1})
      for (auto target : {UINT64_C(0x0000003ffffff800), UINT64_C(0xffffffc000000800)})
        for (bool replay : {false, true}) run_prefix(pc, target, replay, false);
    // Crossing the positive compact limit faults at the exact, nonrepresentable continuation PC.
    run_prefix(UINT64_C(0x0000003ffffffffe), 0x800, false, true);
    run_prefix(UINT64_MAX - 1, 0x800, false, true);

    // A canonical redirected address can still have an independent translation fault.
    initialize(0x106, 0x800, false, false);
    fault_pc = UINT64_C(0xffffffc012340200);
    inject_fault = true;
    redirect_in = {1, {prefix_pc, fault_pc, {0, 0, 0, {}}}};
    virtual_lookup_in.pready = 1;
    settle();
    CHECK(virtual_lookup_out.pvalid && virtual_lookup_out.pbits == fault_pc);
    cycle(); redirect_in = {};
    unsigned waited = 0;
    while (!instructions_out.pvalid && waited++ < 40) cycle();
    CHECK(instructions_out.pvalid);
    for (unsigned i = 0; i < 4; ++i) {
      const auto instruction = instructions_out.pbits.pentries[0];
      CHECK(instruction.ppc == fault_pc && parcel_fault_address(instruction) == fault_pc &&
            instruction.pfault.pvalid && instruction.pfault.pcause == 12 && !instruction.pprediction.pvalid);
      cycle();
    }
  });
}
