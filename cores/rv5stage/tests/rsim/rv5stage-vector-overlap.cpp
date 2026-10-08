// Preserves the rv5stage-vector-overlap cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
// Checks registered sequencing handoff, autonomous splat drain, row hazards, ownership, and replay.

int cycle = 0, phase = 0, done_count = 0, retired_count = 0, fp_count = 0,
    memory_count = 0, store_count = 0, reduction_retirements = 0;

int phase1_issue_count = 0, phase1_last_issue = 0;

int phase1_launches = 0, phase1_sequences = 0;

int phase5_younger_attempts = 0;

int phase10_attempts = 0, phase11_attempts = 0, older_issue_count = 0,
    previous_stores = 0, previous_retirements = 0;

int phase14_attempts = 0, phase14_older_last = 0, phase14_younger = 0;

int overlap_older_attempts = 0, overlap_memory_attempts = 0;

int independent_packed_attempts = 0;

int splat_older_last = 0, splat_first_attempt = 0;

std::uint64_t overlap_older_context = 0, overlap_memory_context = 0;

int fp_tags[8], memory_tags[8];

std::uint64_t stores[8];

bool launch_seen, sequence_done_seen, issue_done_seen,
    tail_handoff_seen = 0, retry_last = 0, retried = 0;

std::uint32_t add_insn_source(int vd, int vs2) {
  return UINT64_C(0x2000057) | (((vs2)&low_mask(32)) << 20) |
         (UINT64_C(4) << 15) | (((vd)&low_mask(32)) << 7);
}
std::uint32_t add_insn(int vd) { return add_insn_source(vd, 2); }

std::uint32_t load_insn(int rd) {
  return UINT64_C(0x2007007) | (((rd)&low_mask(32)) << 7);
}
std::uint32_t store_insn(int rs) {
  return UINT64_C(0x2007027) | (((rs)&low_mask(32)) << 7);
}
std::uint32_t splat_insn(int rd, std::uint8_t masked = 0) {
  return UINT64_C(0x8007007) | (masked ? UINT64_C(0) : UINT64_C(0x2000000)) |
         (((rd)&low_mask(32)) << 7);
}
std::uint32_t reduction_insn(int vd) {
  return UINT64_C(0x2002057) | (UINT64_C(8) << 20) | (UINT64_C(3) << 15) |
         (((vd)&low_mask(32)) << 7);
}
std::uint32_t index_insn(int vd) {
  return ((UINT64_C(20) << 26 | UINT64_C(1) << 25 | UINT64_C(17) << 15 |
           UINT64_C(2) << 12 | ((vd)&low_mask(32)) << 7 | UINT64_C(87)) &
          low_mask(32));
}
std::uint32_t gather_insn(int vd) {
  return ((UINT64_C(12) << 26 | UINT64_C(1) << 25 | UINT64_C(2) << 20 |
           UINT64_C(3) << 15 | ((vd)&low_mask(32)) << 7 | UINT64_C(87)) &
          low_mask(32));
}
void tick() {
  settle();
  hit_data = UINT64_C(4096) + attempt_out.pbits.paddress;
  retry = retry_last && !retried && attempt_out.pvalid &&
          attempt_out.pbits.pmemory && attempt_out.pbits.plast;
  settle();
  launch_seen = request_valid && request_ready;
  sequence_done_seen = sequencing_finished;
  issue_done_seen = issue_finished;
  if (!reset) {
    if (phase == 1 && sequencing_finished && launch_seen)
      tail_handoff_seen = 1;
    if (phase == 1) {
      if (sequencing_finished) {
        CHECK(phase1_sequences < phase1_launches);
        phase1_sequences++;
      }
      if (launch_seen)
        phase1_launches++;
    }
    if (issue_finished)
      done_count++;
    if (retired)
      retired_count++;
    if (phase == 1 && issued) {
      if (phase1_issue_count != 0)
        CHECK(cycle == phase1_last_issue + 1);
      phase1_last_issue = cycle;
      phase1_issue_count++;
    }
    if (fp_request_out.pvalid && fp_request_in.pready) {
      CHECK(fp_count < 8);
      fp_tags[fp_count++] = int(fp_request_out.pbits.ptag);
    }
    if (attempt_out.pvalid && !cancel) {
      if (phase == 20 && attempt_out.pbits.pcontext == UINT64_C(384))
        independent_packed_attempts++;
      if (phase == 14) {
        if (attempt_out.pbits.pcontext == UINT64_C(3584)) {
          CHECK(phase14_attempts < 2);
          if (attempt_out.pbits.plast)
            phase14_older_last = cycle;
        } else {
          CHECK(attempt_out.pbits.pcontext == UINT64_C(3600) &&
                phase14_attempts == 2);
          phase14_younger = cycle;
        }
        phase14_attempts++;
      }
      if (phase == 10) {
        CHECK(phase10_attempts < 2);
        CHECK(attempt_out.pbits.pcontext ==
              (phase10_attempts == 0 ? UINT64_C(2560) : UINT64_C(2688)));
        phase10_attempts++;
      }
      if (phase == 11) {
        CHECK(phase11_attempts < 2);
        CHECK(attempt_out.pbits.pcontext ==
              (phase11_attempts == 0 ? UINT64_C(2816) : UINT64_C(2944)));
        phase11_attempts++;
      }
      if (phase == 18 || phase == 19 || phase == 25) {
        if (attempt_out.pbits.pcontext == overlap_older_context) {
          CHECK(!attempt_out.pbits.pmemory && overlap_memory_attempts == 0 &&
                overlap_older_attempts < 2);
          overlap_older_attempts++;
          if (phase == 25 && attempt_out.pbits.plast)
            splat_older_last = cycle;
        } else {
          CHECK(attempt_out.pbits.pcontext == overlap_memory_context &&
                attempt_out.pbits.pmemory && overlap_older_attempts == 2);
          overlap_memory_attempts++;
          if (phase == 25 && overlap_memory_attempts == 1)
            splat_first_attempt = cycle;
        }
      }
      if (retry)
        retried = 1;
      else if (attempt_out.pbits.pmemory) {
        if (phase == 5 && attempt_out.pbits.paddress >= UINT64_C(384) &&
            attempt_out.pbits.paddress < UINT64_C(512))
          phase5_younger_attempts++;
        if (slow)
          memory_tags[memory_count++] = int(attempt_out.pbits.pcompletion_utag);
        if (attempt_out.pbits.pcontext >= UINT64_C(768))
          stores[store_count++] = attempt_out.pbits.pstore_udata;
      }
    }
  }
  rising();
  falling();
  cycle++;
  CHECK(cycle < 3000);
}
void launch(std::uint32_t insn, std::uint64_t base, std::uint8_t packed_mode) {
  instruction = insn;
  scalar = base;
  packed_memory = packed_mode;
  request_valid = 1;
  do
    tick();
  while (!launch_seen);
  request_valid = 0;
}
void drain() {
  do
    tick();
  while (active);
}
void return_fp(int index, std::uint64_t value) {
  fp_result_in = {};
  fp_result_in.pvalid = 1;
  fp_result_in.pbits.ptag = ((fp_tags[index]) & low_mask(3));
  fp_result_in.pbits.pfp_uvalue = value;
  fp_result_in.pbits.pexception_uflags_uvalid = 1;
  settle();
  CHECK(fp_result_out.pready);
  tick();
  fp_result_in = {};
}
void return_memory(int index, std::uint64_t value) {
  response_in.pvalid = 1;
  response_in.pbits.ptag = ((memory_tags[index]) & low_mask(3));
  response_in.pbits.pdata = value;
  settle();
  while (!response_out.pready) {
    tick();
    settle();
  }
  tick();
  response_in = {};
}

void drive() {}

void observe() {}

void falling_update() {}

void stimulus() {
  reset = 1;
  instruction = 0;
  vl = 2;
  vtype = 24;
  scalar = 0;
  hit_data = 0;
  request_valid = 0;
  packed_memory = 0;
  retry = 0;
  slow = 0;
  cancel = 0;
  issue_ready = 1;
  {
    response_in = {};
    fp_result_in = {};
    fp_request_in.pready = 1;
    tick();
    reset = 0;

    // Blocking scheduling holds the current descriptor before its VRF read;
    // releasing it still permits tail replacement without an issue bubble.
    phase = 1;
    vl = 1;
    issue_ready = 0;
    launch(add_insn(8), UINT64_C(0), 0);
    instruction = add_insn(9);
    scalar = UINT64_C(16);
    request_valid = 1;
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index) {
      tick();
      CHECK(!launch_seen && !sequencing_finished && !issued);
    }
    issue_ready = 1;
    do
      tick();
    while (!launch_seen);
    request_valid = 0;
    // Continue beyond the owner-ring depth to check zero-dead-time handoff.
    for (int destination = 10; destination < 24; destination++)
      // v17 reuses v9's completion slot; v18 must not see its old write metadata.
      launch(add_insn_source(destination, destination == 18 ? 9 : 2),
             ((destination - 8) & low_mask(64)) << 4, 0);
    drain();
    CHECK(tail_handoff_seen && phase1_sequences == 16 &&
          phase1_issue_count == 16 && done_count == 16 && retired_count == 16);
    done_count = 0;
    retired_count = 0;
    vl = 2;
    phase = 2;

    launch(load_insn(8), UINT64_C(256), 0);
    drain();
    launch(load_insn(10), UINT64_C(384), 1);
    drain();

    // Packed admission replaces the older FP descriptor on its final S1
    // transfer, before that beat issues. The dependent store then chains
    // on each completed 64-bit register row.
    phase = 3;
    launch(UINT64_C(0x2001057) | (UINT64_C(8) << 20) | (UINT64_C(10) << 15) |
               (UINT64_C(12) << 7),
           UINT64_C(512), 0);
    instruction = store_insn(12);
    scalar = UINT64_C(768);
    packed_memory = 1;
    request_valid = 1;
    do {
      tick();
      CHECK(!launch_seen || (sequence_done_seen && done_count == 2));
    } while (!launch_seen);
    request_valid = 0;
    CHECK(active);
    for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
      tick();
    CHECK(fp_count == 2);
    CHECK(store_count == 0);
    return_fp(0, UINT64_C(0x1111222233334444));
    while (store_count < 1)
      tick();
    CHECK(active && stores[0] == UINT64_C(0x1111222233334444));
    for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
      tick();
    CHECK(store_count == 1);
    retry_last = 1;
    return_fp(1, UINT64_C(0x5555666677778888));
    drain();
    CHECK(retried && store_count == 2 &&
          stores[1] == UINT64_C(0x5555666677778888));
    retry_last = 0;

    // A packed load tail keeps its alignment/route metadata when the sequencer
    // switches to an ordinary store. Return younger data first; VRF drain and
    // store operands must still be ordered and associated with the old owner.
    phase = 4;
    memory_count = 0;
    store_count = 0;
    slow = 1;
    launch(load_insn(14), UINT64_C(256), 1);
    while (memory_count < 2)
      tick();
    slow = 0;
    launch(store_insn(14), UINT64_C(896), 0);
    for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
      tick();
    CHECK(store_count == 0);
    return_memory(1, UINT64_C(0xfedcba9876543210));
    for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
      tick();
    CHECK(store_count == 0);
    return_memory(0, UINT64_C(0x123456789abcdef));
    drain();
    CHECK(store_count == 2 && stores[0] == UINT64_C(0x123456789abcdef) &&
          stores[1] == UINT64_C(0xfedcba9876543210));

    // A younger load may enter the sequencer, but cannot issue a conflicting
    // destination row until the older response has written that row.
    // This also exercises allocator wrap without a per-launch reset.
    phase = 5;
    memory_count = 0;
    store_count = 0;
    slow = 1;
    launch(load_insn(16), UINT64_C(256), 0);
    while (memory_count < 2)
      tick();
    slow = 0;
    launch(load_insn(16), UINT64_C(384), 1);
    for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
      tick();
    CHECK(phase5_younger_attempts == 0);
    return_memory(1, UINT64_C(48059));
    return_memory(0, UINT64_C(43690));
    drain();
    launch(store_insn(16), UINT64_C(1152), 0);
    drain();
    CHECK(store_count == 2 && stores[0] == UINT64_C(4480) &&
          stores[1] == UINT64_C(4488) && retired_count == 9);

    // Cancellation preserves an accepted prefix even when it ends in a
    // partial VRF row rather than the descriptor's original final word.
    phase = 6;
    vl = 16;
    vtype = 0;
    memory_count = 0;
    slow = 1;
    launch(UINT64_C(0x2000007) | (UINT64_C(18) << 7), UINT64_C(259), 1);
    while (memory_count < 1)
      tick();
    cancel = 1;
    tick();
    cancel = 0;
    slow = 0;
    return_memory(0, UINT64_C(0x706050403020100));
    drain();
    CHECK(retired_count == 9);
    vl = 1;
    vtype = 24;
    store_count = 0;
    launch(store_insn(18), UINT64_C(1024), 0);
    drain();
    CHECK(store_count == 1 &&
          bit_slice(stores[0], 0, 40) == UINT64_C(0x706050403) &&
          retired_count == 10);
    // Each returning read must keep its own SEW and immediate, even though
    // the sequencer has already captured a differently configured successor.
    phase = 7;
    vl = 2;
    vtype = 24;
    for (int rd = 20; rd <= 23; rd++) {
      launch(UINT64_C(0x5e003057) | (((rd)&low_mask(32)) << 7), UINT64_C(64),
             0);
      drain();
    }
    vl = 1;
    for (int n = 0; n < 4; n++) {
      vtype = UINT64_C(24) - (((n)&low_mask(64)) << 3);
      launch(UINT64_C(0x5e003057) | (((n + 1) & low_mask(32)) << 15) |
                 (((20 + n) & low_mask(32)) << 7),
             UINT64_C(80) + ((n)&low_mask(64)), 0);
    }
    drain();
    vtype = 24;
    store_count = 0;
    for (int rd = 20; rd <= 23; rd++) {
      launch(store_insn(rd), UINT64_C(1280), 0);
      drain();
    }
    CHECK(store_count == 4 && stores[0] == 1 && stores[1] == 2 &&
          stores[2] == 3 && stores[3] == 4);
    // Cancellation kills a scheduled read before its fixed response issues.
    phase = 8;
    issue_ready = 1;
    launch(add_insn(20), UINT64_C(96), 0);
    tick();
    issue_ready = 0;
    cancel = 1;
    tick();
    cancel = 0;
    issue_ready = 1;
    drain();
    // A reduction retains only owner-local recurrence and completion state.
    // Its tail hands off the sequencer before the final result retires.
    phase = 9;
    vl = 2;
    vtype = 24;
    reduction_retirements = retired_count;
    launch(reduction_insn(24), UINT64_C(2304), 0);
    launch(add_insn(25), UINT64_C(2320), 0);
    CHECK(retired_count == reduction_retirements);
    drain();
    CHECK(retired_count == reduction_retirements + 2);
    // A packed successor may enter the sequencer as the older compute launches
    // its final VRF read, but cannot take the older beat's completion slot.
    reset = 1;
    tick();
    reset = 0;
    phase = 0;
    vl = 1;
    vtype = 24;
    launch(add_insn(26), UINT64_C(2432), 0);
    drain();
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      tick();
    phase = 10;
    older_issue_count = done_count;
    launch(add_insn(27), UINT64_C(2560), 0);
    launch(load_insn(28), UINT64_C(2688), 1);
    CHECK(done_count == older_issue_count);
    drain();
    CHECK(phase10_attempts == 2);
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      tick();
    phase = 11;
    older_issue_count = done_count;
    previous_stores = store_count;
    launch(add_insn(29), UINT64_C(2816), 0);
    launch(store_insn(26), UINT64_C(2944), 1);
    CHECK(done_count == older_issue_count);
    drain();
    CHECK(phase11_attempts == 2 && store_count == previous_stores + 1);
    // Index generation has no cross-beat carry. Its two-beat tail must hand
    // off the sequencer before the final beat reaches the issue boundary.
    reset = 1;
    tick();
    reset = 0;
    phase = 12;
    vl = 16;
    vtype = 0;
    older_issue_count = done_count;
    launch(index_insn(8), UINT64_C(3072), 0);
    instruction = add_insn(9);
    scalar = UINT64_C(3088);
    request_valid = 1;
    do
      tick();
    while (!launch_seen);
    CHECK(sequence_done_seen && !issue_done_seen);
    request_valid = 0;
    drain();
    CHECK(done_count == older_issue_count + 2);
    // Index follows the older compute without depending on a buffered read.
    reset = 1;
    tick();
    reset = 0;
    phase = 13;
    vl = 1;
    vtype = 24;
    issue_ready = 1;
    older_issue_count = done_count;
    launch(add_insn(8), UINT64_C(3328), 0);
    instruction = index_insn(9);
    scalar = UINT64_C(3344);
    request_valid = 1;
    do
      tick();
    while (!launch_seen);
    request_valid = 0;
    drain();
    CHECK(done_count == older_issue_count + 2);
    // The older two-row compute keeps the second row pending, but its first
    // row should stop blocking a younger read as soon as that row drains.
    reset = 1;
    tick();
    reset = 0;
    phase = 14;
    vl = 16;
    vtype = 0;
    launch(add_insn(8), UINT64_C(3584), 0);
    vl = 8;
    launch(add_insn_source(9, 8), UINT64_C(3600), 0);
    drain();
    while (phase14_attempts < 3)
      tick();
    CHECK(phase14_attempts == 3 && phase14_older_last > 0 &&
          phase14_younger > phase14_older_last);
    CHECK(phase14_younger - phase14_older_last <= 2);
    // A delayed FP result must not hold an unrelated local result behind it.
    // Conversely, a younger writer of the same row must wait for that result.
    reset = 1;
    tick();
    reset = 0;
    phase = 15;
    vl = 1;
    vtype = 24;
    fp_count = 0;
    store_count = 0;
    launch(UINT64_C(0x2001057) | (UINT64_C(8) << 20) | (UINT64_C(10) << 15) |
               (UINT64_C(12) << 7),
           UINT64_C(3840), 0);
    while (fp_count < 1)
      tick();
    launch(index_insn(14), UINT64_C(3856), 0);
    for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
      tick();
    launch(store_insn(14), UINT64_C(3904), 0);
    while (store_count < 1)
      tick();
    CHECK(active && stores[0] == 0);
    older_issue_count = done_count;
    launch(index_insn(12), UINT64_C(3872), 0);
    for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index)
      tick();
    CHECK(done_count == older_issue_count);
    return_fp(0, UINT64_C(0x1111222233334444));
    drain();
    launch(store_insn(12), UINT64_C(3968), 0);
    drain();
    CHECK(store_count == 2 && stores[1] == 0);
    // The final authorized memory beat releases the registered sequencer and
    // admits its successor on that edge, not one cycle after feedback.
    reset = 1;
    tick();
    reset = 0;
    phase = 16;
    vl = 1;
    vtype = 24;
    launch(load_insn(8), UINT64_C(4096), 0);
    instruction = load_insn(10);
    scalar = UINT64_C(4112);
    request_valid = 1;
    do
      tick();
    while (!launch_seen);
    CHECK(sequence_done_seen);
    request_valid = 0;
    drain();
    // A rejected tail still owns its checkpoint, so it cannot hand off until
    // the replayed tail is authorized.
    reset = 1;
    tick();
    reset = 0;
    phase = 17;
    retried = 0;
    retry_last = 1;
    launch(load_insn(8), UINT64_C(4352), 0);
    instruction = load_insn(10);
    scalar = UINT64_C(4368);
    request_valid = 1;
    do {
      tick();
      if (retry)
        CHECK(!launch_seen);
    } while (!launch_seen);
    CHECK(retried && sequence_done_seen);
    request_valid = 0;
    retry_last = 0;
    drain();
    // Elementwise memory may capture the sequencer on an older compute tail
    // read while its previous beat is issuing and its final read is in flight.
    reset = 1;
    tick();
    reset = 0;
    phase = 18;
    vl = 2;
    vtype = 24;
    issue_ready = 1;
    retried = 0;
    retry_last = 1;
    overlap_older_context = UINT64_C(544);
    overlap_memory_context = UINT64_C(560);
    overlap_older_attempts = 0;
    overlap_memory_attempts = 0;
    older_issue_count = done_count;
    previous_retirements = retired_count;
    launch(add_insn(8), overlap_older_context, 0);
    instruction = load_insn(10);
    scalar = overlap_memory_context;
    request_valid = 1;
    do
      tick();
    while (!sequence_done_seen);
    request_valid = 0;
    drain();
    CHECK(retried && overlap_older_attempts == 2 &&
          overlap_memory_attempts == 3 && done_count == older_issue_count + 2 &&
          retired_count == previous_retirements + 2);
    retry_last = 0;
    // Gather's dependent second read must stay ordered before the younger
    // direct memory read without a post-read issue queue.
    reset = 1;
    tick();
    reset = 0;
    phase = 19;
    vl = 2;
    vtype = 24;
    issue_ready = 1;
    overlap_older_context = UINT64_C(576);
    overlap_memory_context = UINT64_C(592);
    overlap_older_attempts = 0;
    overlap_memory_attempts = 0;
    older_issue_count = done_count;
    previous_retirements = retired_count;
    launch(gather_insn(8), overlap_older_context, 0);
    instruction = load_insn(10);
    scalar = overlap_memory_context;
    request_valid = 1;
    do
      tick();
    while (!sequence_done_seen);
    request_valid = 0;
    drain();
    CHECK(overlap_older_attempts == 2 && overlap_memory_attempts == 2 &&
          done_count == older_issue_count + 2 &&
          retired_count == previous_retirements + 2);
    // The first authorized zero-stride read releases sequencing even while its
    // response is held. Independent packed work proceeds; a consumer still
    // waits for both splat rows, with no dependency on the new sequencing ID.
    reset = 1;
    tick();
    reset = 0;
    phase = 20;
    vl = 2;
    vtype = 24;
    memory_count = 0;
    store_count = 0;
    slow = 1;
    previous_retirements = retired_count;
    launch(splat_insn(8), UINT64_C(256), 0);
    instruction = load_insn(10);
    scalar = UINT64_C(384);
    packed_memory = 1;
    request_valid = 1;
    do
      tick();
    while (!launch_seen);
    CHECK(sequence_done_seen && memory_count == 1);
    request_valid = 0;
    slow = 0;
    for (unsigned repeat_index = 0; repeat_index < (10); ++repeat_index)
      tick();
    CHECK(independent_packed_attempts == 2 &&
          retired_count == previous_retirements);
    launch(store_insn(8), UINT64_C(1024), 0);
    for (unsigned repeat_index = 0; repeat_index < (6); ++repeat_index)
      tick();
    CHECK(store_count == 0);
    return_memory(0, UINT64_C(0xfeedface12345678));
    drain();
    CHECK(store_count == 2 && stores[0] == UINT64_C(0xfeedface12345678) &&
          stores[1] == UINT64_C(0xfeedface12345678));
    launch(store_insn(10), UINT64_C(896), 0);
    drain();
    CHECK(store_count == 4 && stores[2] == UINT64_C(4480) &&
          stores[3] == UINT64_C(4488));

    // A younger destination intent must wait for the splat, not deadlock its
    // older writer. Repeat across owner-ring wrap and verify the younger wins.
    for (int pass = 0; pass < 4; pass++) {
      phase = 21;
      memory_count = 0;
      store_count = 0;
      slow = 1;
      launch(splat_insn(8), UINT64_C(256), 0);
      while (memory_count < 1)
        tick();
      slow = 0;
      older_issue_count = done_count;
      launch(index_insn(8), UINT64_C(512), 0);
      for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
        tick();
      CHECK(done_count == older_issue_count);
      return_memory(0, UINT64_C(0xffffffffffffffff));
      drain();
      launch(store_insn(8), UINT64_C(1024), 0);
      drain();
      CHECK(store_count == 2 && stores[0] == 0 && stores[1] == 1);
    }

    // Mask words remain read-owned by the old splat after its read is accepted.
    // A younger v0 writer cannot clobber them or block that owner's drain.
    phase = 22;
    slow = 0;
    launch(UINT64_C(0x5e003057) | (UINT64_C(31) << 15), UINT64_C(64), 0);
    drain();
    memory_count = 0;
    store_count = 0;
    slow = 1;
    launch(splat_insn(8, 1), UINT64_C(256), 0);
    while (memory_count < 1)
      tick();
    slow = 0;
    older_issue_count = done_count;
    launch(index_insn(0), UINT64_C(512), 0);
    for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
      tick();
    CHECK(done_count == older_issue_count);
    return_memory(0, UINT64_C(0xabcdef));
    drain();
    launch(store_insn(8), UINT64_C(1024), 0);
    drain();
    CHECK(store_count == 2 && stores[0] == UINT64_C(0xabcdef) &&
          stores[1] == UINT64_C(0xabcdef));

    // The one-value splat engine still backpressures a second splat, rather
    // than replacing the pending response's descriptor or destination.
    phase = 23;
    memory_count = 0;
    store_count = 0;
    slow = 1;
    launch(splat_insn(8), UINT64_C(256), 0);
    while (memory_count < 1)
      tick();
    instruction = splat_insn(10);
    scalar = UINT64_C(384);
    request_valid = 1;
    slow = 0;
    for (int repeat_index = 0; repeat_index < (5); ++repeat_index) {
      tick();
      CHECK(!launch_seen);
    }
    return_memory(0, UINT64_C(4660));
    do
      tick();
    while (!launch_seen);
    request_valid = 0;
    drain();
    launch(store_insn(8), UINT64_C(1024), 0);
    drain();
    launch(store_insn(10), UINT64_C(1152), 0);
    drain();
    CHECK(store_count == 4 && stores[0] == UINT64_C(4660) &&
          stores[1] == UINT64_C(4660) && stores[2] == UINT64_C(4480) &&
          stores[3] == UINT64_C(4480));

    // A splat that has released sequencing still waits for older writers of
    // its later rows. The younger store may consume each completed splat row.
    reset = 1;
    tick();
    reset = 0;
    phase = 24;
    fp_count = 0;
    memory_count = 0;
    store_count = 0;
    slow = 1;
    launch(UINT64_C(0x2001057) | (UINT64_C(12) << 20) | (UINT64_C(14) << 15) |
               (UINT64_C(8) << 7),
           UINT64_C(512), 0);
    while (fp_count < 2)
      tick();
    launch(splat_insn(8), UINT64_C(256), 0);
    for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
      tick();
    CHECK(memory_count == 0);
    return_fp(0, UINT64_C(4369));
    while (memory_count < 1)
      tick();
    slow = 0;
    launch(store_insn(8), UINT64_C(1024), 0);
    return_memory(0, UINT64_C(64206));
    while (store_count < 1)
      tick();
    for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
      tick();
    CHECK(store_count == 1 && stores[0] == UINT64_C(64206));
    return_fp(1, UINT64_C(8738));
    drain();
    CHECK(store_count == 2 && stores[1] == UINT64_C(64206));

    // Capture a splat on the older compute's final S1 edge, while that
    // compute still has a registered operand response to issue. The splat's
    // first attempt follows it without a bubble; neither captures the other's
    // descriptor or owner. Also retry a masked splat after this handoff.
    for (int masked = 0; masked < 2; masked++) {
      reset = 1;
      tick();
      reset = 0;
      phase = 0;
      vl = 2;
      vtype = 24;
      slow = 0;
      launch(UINT64_C(0x5e003057) | (UINT64_C(31) << 15), UINT64_C(64), 0);
      drain();
      // The delayed public attempt observer outlives local-compute retirement.
      for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
        tick();
      phase = 25;
      overlap_older_context = UINT64_C(544);
      overlap_memory_context = UINT64_C(560);
      overlap_older_attempts = 0;
      overlap_memory_attempts = 0;
      splat_older_last = 0;
      splat_first_attempt = 0;
      retried = 0;
      retry_last = ((masked)&low_mask(1));
      older_issue_count = done_count;
      previous_retirements = retired_count;
      launch(index_insn(8), overlap_older_context, 0);
      instruction = splat_insn(10, ((masked)&low_mask(1)));
      scalar = overlap_memory_context;
      request_valid = 1;
      do
        tick();
      while (!launch_seen);
      CHECK(sequence_done_seen && done_count == older_issue_count);
      request_valid = 0;
      drain();
      CHECK(overlap_older_attempts == 2 &&
            overlap_memory_attempts == 1 + masked &&
            retried == ((masked)&low_mask(1)));
      CHECK(splat_first_attempt == splat_older_last + 1);
      CHECK(done_count == older_issue_count + 2 &&
            retired_count == previous_retirements + 2);
      phase = 0;
      retry_last = 0;
      store_count = 0;
      launch(store_insn(8), UINT64_C(1024), 0);
      drain();
      launch(store_insn(10), UINT64_C(1152), 0);
      drain();
      CHECK(store_count == 4 && stores[0] == 0 && stores[1] == 1 &&
            stores[2] == UINT64_C(4656) && stores[3] == UINT64_C(4656));
    }

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
