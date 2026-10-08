// Preserves the rv5stage-vector-admission cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
using RiscvSplitResult =
    std::remove_cvref_t<decltype(split_completion_in.pbits)>;
using PhysicalMemoryReq =
    std::remove_cvref_t<decltype(memory_requests_out.pbits)>;
using RiscvSplitResult =
    std::remove_cvref_t<decltype(split_completion_in.pbits)>;
// Checks vector admission, certification, fallback memory, and misaligned slow requests.

int cycle = 0, launches = 0, completions = 0, outcomes = 0, checks = 0,
    releases = 0, accesses = 0, memory_requests = 0;

std::uint64_t addresses[256], data[256], last_outcome;

int access_cycles[256];

PhysicalMemoryReq pending_memory[8];

bool accepted;

bool watch_window = 0;

int expected_window_accesses = 0;

std::uint32_t move_insn(int rd, int value) {
  return UINT64_C(0x5e003057) | (((rd)&low_mask(32)) << 7) |
         (((value)&low_mask(32)) << 15);
}
std::uint32_t memory_insn(int rd, std::uint8_t store) {
  return (store ? UINT64_C(0x2007027) : UINT64_C(0x2007007)) |
         (((rd)&low_mask(32)) << 7);
}
std::uint32_t load16_insn(int rd) {
  return UINT64_C(0x2005007) | (((rd)&low_mask(32)) << 7);
}
std::uint32_t zero_stride_load16_insn(int rd) {
  return UINT64_C(0xa005007) | (((rd)&low_mask(32)) << 7);
}
void tick() {
  settle();
  accepted = request_valid && request_ready;
  if (!reset) {
    if (accepted)
      launches++;
    if (retired)
      completions++;
    if (outcome_valid) {
      outcomes++;
      last_outcome = outcome_pc;
      if (outcome_pc == UINT64_C(768) || outcome_pc == UINT64_C(1024))
        CHECK(!certification_pending && !request_ready);
    }
    if (precheck_out.prequest.pvalid && precheck_in.prequest.pready)
      checks++;
    if (precheck_out.prelease.pvalid)
      releases++;
    if (watch_window && precheck_out.prelease.pvalid)
      CHECK(accesses >= expected_window_accesses);
    if (accesses_out.pvalid) {
      CHECK(accesses < 256);
      addresses[accesses] = accesses_out.pbits.paddress;
      access_cycles[accesses] = cycle;
      data[accesses] = accesses_out.pbits.pdata;
      accesses++;
    }
    if (memory_requests_out.pvalid) {
      CHECK(memory_requests < 8);
      pending_memory[memory_requests++] = memory_requests_out.pbits;
    }
  }
  rising();
  falling();
  cycle++;
  CHECK(cycle < 4000);
}
void clear() {
  reset = 1;
  request_valid = 0;
  fast_valid = 0;
  retry_last = 0;
  retry_address = 0;
  slow_access = 0;
  watch_window = 0;
  precheck_in = {};
  memory_responses_in = {};
  split_completion_in = {};
  tick();
  reset = 0;
  launches = 0;
  completions = 0;
  outcomes = 0;
  checks = 0;
  releases = 0;
  accesses = 0;
  memory_requests = 0;
  settle();
  CHECK(!active && !loads_pending && !stores_pending && !fp_pending &&
        !certification_pending);
}
void launch(std::uint32_t insn, std::uint64_t pc, std::uint64_t length,
            std::uint64_t configuration) {
  instruction = insn;
  scalar = pc;
  vl = length;
  vtype = configuration;
  request_valid = 1;
  do
    tick();
  while (!accepted);
  request_valid = 0;
}
void drain() {
  // The existing store-drain barrier clears after the last macro retires.
  do
    tick();
  while (active || stores_pending);
  CHECK(completions == launches);
  CHECK(!loads_pending && !stores_pending && !fp_pending &&
        !certification_pending);
}
void certify(std::uint64_t first_address, std::uint64_t last_address,
             std::uint8_t safe) {
  while (!precheck_out.prequest.pvalid)
    tick();
  for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
    settle();
    CHECK(precheck_out.prequest.pvalid &&
          precheck_out.prequest.pbits.pfirst == first_address &&
          precheck_out.prequest.pbits.plast == last_address);
    CHECK(certification_pending && !request_ready);
    tick();
  }
  precheck_in.prequest.pready = 1;
  tick();
  precheck_in.prequest.pready = 0;
  for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
    CHECK(!precheck_out.prequest.pvalid && !request_ready);
    tick();
  }
  precheck_in.presponse = {.pvalid = 1, .pbits = safe};
  settle();
  CHECK(precheck_out.presponse.pready);
  tick();
  precheck_in.presponse = {};
}
void return_memory(int index) {
  if ((pending_memory[index].paddress &
       ((UINT64_C(1) << pending_memory[index].pwidth) - 1)) != 0) {
    split_completion_in = {};
    split_completion_in.pvalid = 1;
    split_completion_in.pbits.presponse.pdata = pending_memory[index].paddress;
    split_completion_in.pbits.presponse.pcontext =
        pending_memory[index].pcontext;
  } else {
    memory_responses_in = {};
    memory_responses_in.pvalid = 1;
    memory_responses_in.pbits.pdata = pending_memory[index].paddress;
    memory_responses_in.pbits.pcontext = pending_memory[index].pcontext;
  }
  tick();
  memory_responses_in = {};
  split_completion_in = {};
}

void drive() {}

void observe() {}

void falling_update() {}

void stimulus() {
  reset = 1;
  instruction = 0;
  vl = 1;
  vtype = 24;
  scalar = 0;
  request_valid = 0;
  fast_valid = 0;
  retry_last = 0;
  retry_address = 0;
  slow_access = 0;
  {
    precheck_in = {};
    clear();
    launch(move_insn(8, 1), UINT64_C(1), 1, 24);
    settle();
    CHECK(sequencing);
    drain();
    clear();
    // One descriptor per cycle, across both queue pointers and owner-ring wrap.
    for (int n = 0; n < 16; n++) {
      instruction = move_insn(n + 8, 1);
      scalar = ((n)&low_mask(64));
      vl = 1;
      vtype = 24;
      request_valid = 1;
      tick();
      CHECK(accepted);
    }
    request_valid = 0;
    drain();

    // Distinct captured mappings need no shared-window installation, and
    // independent loads start on consecutive cycles before either hit returns.
    clear();
    fast_valid = 1;
    launch(memory_insn(8, 0), UINT64_C(4096), 1, 24);
    launch(memory_insn(16, 0), UINT64_C(8192), 1, 24);
    fast_valid = 0;
    drain();
    CHECK(checks == 0 && outcomes == 0 && releases == 0 && accesses == 2 &&
          addresses[0] == UINT64_C(0x11000) &&
          addresses[1] == UINT64_C(0x12000) &&
          access_cycles[1] == access_cycles[0] + 1);
    fast_valid = 1;
    launch(memory_insn(8, 1), UINT64_C(12288), 1, 24);
    launch(memory_insn(16, 1), UINT64_C(16384), 1, 24);
    fast_valid = 0;
    drain();
    CHECK(accesses == 4 && addresses[2] == UINT64_C(0x13000) &&
          addresses[3] == UINT64_C(0x14000) && data[2] == UINT64_C(0x11000) &&
          data[3] == UINT64_C(0x12000));

    // A cache rejection must retry retained requests, not flush the successor.
    clear();
    fast_valid = 1;
    retry_last = 1;
    retry_address = UINT64_C(0x11000);
    launch(memory_insn(8, 0), UINT64_C(4096), 1, 24);
    launch(memory_insn(16, 0), UINT64_C(8192), 1, 24);
    fast_valid = 0;
    launch(move_insn(24, 7), UINT64_C(48), 1, 24);
    drain();
    CHECK(outcomes == 0 && checks == 0 && releases == 0 && accesses == 4 &&
          addresses[0] == UINT64_C(0x11000) &&
          addresses[1] == UINT64_C(0x12000) &&
          addresses[2] == UINT64_C(0x11000) &&
          addresses[3] == UINT64_C(0x12000));

    clear();
    fast_valid = 1;
    retry_last = 1;
    retry_address = UINT64_C(0x11008);
    launch(memory_insn(8, 0), UINT64_C(4096), 2, 24);
    launch(memory_insn(16, 0), UINT64_C(8192), 1, 24);
    fast_valid = 0;
    drain();
    CHECK(accesses == 5 && addresses[0] == UINT64_C(0x11000) &&
          addresses[1] == UINT64_C(0x11008) &&
          addresses[2] == UINT64_C(0x12000) &&
          addresses[3] == UINT64_C(0x11008) &&
          addresses[4] == UINT64_C(0x12000));

    // Pending slow responses do not retain sequencing ownership. Both macros
    // use their own physical page even with multiple unresolved returns.
    clear();
    fast_valid = 1;
    slow_access = 1;
    launch(memory_insn(8, 0), UINT64_C(4096), 1, 24);
    launch(memory_insn(16, 0), UINT64_C(8192), 1, 24);
    fast_valid = 0;
    launch(move_insn(24, 7), UINT64_C(48), 1, 24);
    while (memory_requests < 2)
      tick();
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      tick();
    CHECK(!sequencing && active &&
          pending_memory[0].paddress == UINT64_C(0x11000) &&
          pending_memory[1].paddress == UINT64_C(0x12000));
    return_memory(1);
    return_memory(0);
    drain();

    // A long current macro blocks a same-destination head. The second waiting
    // entry is FP: its status must be visible before it reaches the sequencer.
    clear();
    launch(move_insn(8, 1), UINT64_C(16), 16, 27);
    launch(move_insn(8, 2), UINT64_C(32), 16, 27);
    launch(UINT64_C(0x2001057) | (UINT64_C(8) << 20) | (UINT64_C(10) << 15) |
               (UINT64_C(24) << 7),
           UINT64_C(48), 1, 24);
    instruction = move_insn(26, 3);
    scalar = UINT64_C(64);
    request_valid = 1;
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
      settle();
      CHECK(!request_ready && fp_pending && !fp_offered);
      tick();
      CHECK(!accepted);
    }
    clear();

    // A full compute queue replaces its departing head without an admission
    // bubble; the producer holds the rejected offer instead of replaying it.
    launch(move_insn(8, 1), UINT64_C(16), 16, 27);
    launch(move_insn(8, 2), UINT64_C(32), 16, 27);
    launch(move_insn(16, 3), UINT64_C(48), 16, 27);
    instruction = move_insn(24, 4);
    scalar = UINT64_C(64);
    vl = 1;
    vtype = 24;
    request_valid = 1;
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
      tick();
      CHECK(!accepted);
    }
    do
      tick();
    while (!accepted);
    request_valid = 0;
    drain();

    // The tail store must be visible immediately, but must not start checking
    // until the preceding compute descriptor has left the queue.
    clear();
    launch(move_insn(8, 1), UINT64_C(16), 16, 27);
    launch(move_insn(16, 2), UINT64_C(32), 16, 27);
    launch(memory_insn(24, 1), UINT64_C(768), 2, 24);
    settle();
    CHECK(stores_pending && !loads_pending && certification_pending &&
          !precheck_out.prequest.pvalid);
    // Live input changes must not alter the captured memory range or its PC.
    instruction = move_insn(4, 7);
    scalar = UINT64_C(57005);
    vl = 1;
    vtype = 0;
    certify(UINT64_C(768), UINT64_C(783), 0);
    CHECK(certification_pending);
    drain();
    CHECK(checks == 1 && releases == 1 && outcomes == 1 &&
          last_outcome == UINT64_C(768) && accesses == 2 &&
          addresses[0] == UINT64_C(768) && addresses[1] == UINT64_C(776));

    // Successful certification permits younger compute admission while the
    // memory owner retains its certificate. Distinct queued operands survive.
    clear();
    launch(memory_insn(8, 0), UINT64_C(1024), 16, 27);
    certify(UINT64_C(1024), UINT64_C(1151), 1);
    while (certification_pending)
      tick();
    launch(move_insn(24, 7), UINT64_C(80), 1, 24);
    launch(move_insn(25, 9), UINT64_C(96), 1, 24);
    drain();
    CHECK(checks == 1 && releases == 1 && outcomes == 1 && accesses == 16 &&
          last_outcome == UINT64_C(1024));
    for (int n = 0; n < 16; n++)
      CHECK(addresses[n] == UINT64_C(1024) + ((n)&low_mask(64)) * 8);
    launch(memory_insn(24, 1), UINT64_C(1280), 1, 24);
    certify(UINT64_C(1280), UINT64_C(1287), 1);
    drain();
    launch(memory_insn(25, 1), UINT64_C(1288), 1, 24);
    certify(UINT64_C(1288), UINT64_C(1295), 1);
    drain();
    CHECK(data[16] == 7 && data[17] == 9);

    // An older delayed load holds completion slots while a compute macro
    // hands off sequencing to a packed load. The new page window must remain
    // held through the packed load's final-beat retry.
    clear();
    slow_access = 1;
    launch(memory_insn(8, 0), UINT64_C(1536), 8, 27);
    certify(UINT64_C(1536), UINT64_C(1599), 1);
    while (memory_requests < 8)
      tick();
    while (releases < 1)
      tick();
    slow_access = 0;
    launch(move_insn(16, 13), UINT64_C(1664), 64, 11);
    watch_window = 1;
    expected_window_accesses = 25;
    retry_last = 1;
    retry_address = UINT64_C(1912);
    launch(load16_insn(24), UINT64_C(1792), 64, 11);
    certify(UINT64_C(1792), UINT64_C(1919), 1);
    for (int n = 0; n < 8; n++)
      return_memory(n);
    drain();
    CHECK(checks == 2 && releases == 2 && accesses == 25 && outcomes == 2 &&
          last_outcome == UINT64_C(1792));
    for (int n = 8; n < 25; n++)
      CHECK(addresses[n] >= UINT64_C(1792) && addresses[n] <= UINT64_C(1919));

    // A zero-stride splat checks one element while older compute still owns
    // the sequencer, then retires before its one data read and row writes.
    clear();
    launch(move_insn(8, 1), UINT64_C(16), 16, 27);
    launch(zero_stride_load16_insn(24), UINT64_C(2048), 4, 8);
    certify(UINT64_C(2048), UINT64_C(2055), 1);
    while (!outcome_valid)
      tick();
    CHECK(!certification_pending && accesses == 0);
    drain();
    CHECK(checks == 1 && releases == 1 && outcomes == 1 &&
          last_outcome == UINT64_C(2048) && accesses == 1 &&
          addresses[0] == UINT64_C(2048));

    clear();
    launch(zero_stride_load16_insn(24), UINT64_C(2304), 4, 8);
    certify(UINT64_C(2304), UINT64_C(2311), 0);
    CHECK(certification_pending);
    drain();
    CHECK(checks == 1 && releases == 1 && outcomes == 1 &&
          last_outcome == UINT64_C(2304) && accesses == 1 &&
          addresses[0] == UINT64_C(2304));

    clear();
    launch(memory_insn(8, 0), UINT64_C(1536), 0, 24);
    drain();
    CHECK(checks == 0 && releases == 0 && accesses == 0 && outcomes == 1 &&
          last_outcome == UINT64_C(1536));
    // Misaligned elements bypass the speculative lookup, retain one vector
    // completion owner, and use the same WB slow offer as scalar accesses.
    clear();
    launch(memory_insn(8, 0), UINT64_C(4099), 1, 24);
    while (memory_requests < 1)
      tick();
    CHECK(checks == 0 && accesses == 0 &&
          pending_memory[0].paddress == UINT64_C(4099) &&
          pending_memory[0].paccess == 1);
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      tick();
    CHECK(active && completions == 0 && outcomes == 0);
    return_memory(0);
    drain();
    launch(memory_insn(8, 1), UINT64_C(8197), 1, 24);
    while (memory_requests < 2)
      tick();
    CHECK(checks == 0 && accesses == 0 &&
          pending_memory[1].paddress == UINT64_C(8197) &&
          pending_memory[1].paccess == 2 &&
          pending_memory[1].pdata == UINT64_C(4099));
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      tick();
    CHECK(active && completions == 1 && outcomes == 1);
    return_memory(1);
    drain();

    throw Finished{};
  }
}

int main() {
  return run_test([] {
    cycle_limit = 1000000;
    try {
      stimulus();
      for (;;)
        rising();
    } catch (const Finished &) {
    }
  });
}
