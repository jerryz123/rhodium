// Preserves the rv5stage-fetch-throughput cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
extern "C" void fetch_trace_init();
extern "C" void fetch_trace_sample(unsigned sample,
                                   std::uint64_t request_address,
                                   std::uint64_t request_line);
extern "C" void fetch_trace_check(unsigned done);
// Checks fetch throughput and exact I-cache lineage across CHI retries, flushes, and reset.

bool done = 0;

int cycle = 0, offset = 0, requests = 0, beat = 0;
bool compressed_mode = 0, stall_mode = 0;
int line_requests[32];
bool pending = 0;
std::uint64_t line_address;
std::uint16_t transaction;
int retry_phase = 0, retry_delay = 0;

std::uint32_t expected_instruction(int index) {
  if (compressed_mode)
    return UINT64_C(19) | ((((index % 31) + 1) & low_mask(32)) << 7);
  return bit_slice(index, 0, 1) ? UINT64_C(32727987) : UINT64_C(22213427);
}
std::uint8_t byte_at(std::uint64_t address) {
  int relative_address;
  std::uint32_t word;
  relative_address = int(address - UINT64_C(4096)) - offset;
  if (relative_address < 0)
    return bit_slice(address, 0, 1) ? UINT64_C(0) : UINT64_C(1);
  if (compressed_mode) {
    word = UINT64_C(16385) |
           (((((relative_address / 2) % 31) + 1) & low_mask(32)) << 7);
    return bit_slice(word, 8 * (relative_address % 2), 8);
  }
  word = expected_instruction(relative_address / 4);
  return bit_slice(word, 8 * (relative_address % 4), 8);
}

void tick() {
  rising();
  settle();
}
void run_stream(std::uint8_t measure) {
  int seen, elapsed, first_cycle, last_cycle, bubbles, refills_before;
  int sampled, previous_delivery, stride, line_instructions;
  bool held;
  std::uint64_t held_pc;
  std::uint32_t held_instruction;
  held = 0;
  previous_delivery = -1;
  stride = compressed_mode ? 2 : 4;
  line_instructions = 64 / stride;
  seen = 0;
  elapsed = 0;
  first_cycle = 0;
  last_cycle = 0;
  bubbles = 0;
  sampled = 0;
  restart = 1;
  active = 1;
  tick();
  restart = 0;
  refills_before = requests;
  while (seen < 256 && elapsed < 20000) {
    falling();
    sink_ready = !stall_mode || cycle % 11 >= 4;
    settle();
    if (held)
      CHECK(valid && pc == held_pc && instruction == held_instruction);
    held = valid && !sink_ready;
    held_pc = pc;
    held_instruction = instruction;
    if (valid) {
      CHECK(pc == start_pc + ((stride * seen) & low_mask(64)) &&
            instruction == expected_instruction(seen) && !fault);
    }
    if (measure && seen >= 32 && seen < 224) {
      if (sampled == 0) {
        first_cycle = cycle;
        refills_before = requests;
      }
      sampled++;
      if (valid) {
        if (last_cycle != 0)
          bubbles += cycle - last_cycle - 1;
        last_cycle = cycle;
      }
      if (valid && seen == 223) {
        ;
        CHECK(requests == refills_before);
        CHECK(bubbles == 0 && last_cycle - first_cycle + 1 == 192);
      }
    }
    if (valid && sink_ready) {
      // Do not restart after warming: measure the interior of every cold
      // line, leaving a small explicit refill-to-delivery recovery allowance.
      if (!measure && !stall_mode && seen % line_instructions >= 4 &&
          seen % line_instructions < line_instructions - 2)
        CHECK(cycle == previous_delivery + 1);
      previous_delivery = cycle;
      seen++;
    }
    tick();
    elapsed++;
  }
  CHECK(seen == 256);
  active = 0;
  for (unsigned repeat_index = 0; repeat_index < (100); ++repeat_index)
    tick();
}

void drive() {
  {
    chi_in = {};
    chi_in.preq.pready = cycle % 4 != 0;
    chi_in.prsp.presponse.pvalid = retry_phase != 0 && retry_delay == 0;
    chi_in.prsp.presponse.pbits.popcode =
        retry_phase == 1 ? UINT64_C(3) : UINT64_C(7);
    chi_in.prsp.presponse.pbits.psrc_uid = UINT64_C(1);
    chi_in.prsp.presponse.pbits.ptgt_uid = UINT64_C(2);
    chi_in.prsp.presponse.pbits.ptxn_uid = transaction;
    chi_in.prsp.presponse.pbits.ppcrd_utype = UINT64_C(2);
    chi_in.prsp.prequester.pready = 1;
    chi_in.pdat.prequest.pready = 1;
    chi_in.pdat.presponse.pvalid = pending && (!stall_mode || cycle % 7 >= 3);
    chi_in.pdat.presponse.pbits.popcode = UINT64_C(4);
    chi_in.pdat.presponse.pbits.presp = UINT64_C(0);
    chi_in.pdat.presponse.pbits.pbyte_uenable = UINT64_C(65535);
    chi_in.pdat.presponse.pbits.pdata_uid = ((beat)&low_mask(2));
    chi_in.pdat.presponse.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
        UINT64_C(1);
    chi_in.pdat.presponse.pbits.pdbid_uor_umecid = UINT64_C(85);
    chi_in.pdat.presponse.pbits.ptxn_uid = transaction;
    chi_in.pdat.presponse.pbits.psrc_uid = UINT64_C(1);
    chi_in.pdat.presponse.pbits.ptgt_uid = UINT64_C(2);
    for (int b = 0; b < 16; b++)
      bit_slice(chi_in.pdat.presponse.pbits.pdata, b * 8, 8) =
          byte_at(line_address + ((16 * beat + b) & low_mask(64)));
  }
}

void observe() {
  fetch_trace_sample(
      int(reset) | (int(trace_admit) << 1) | (int(trace_clear) << 2) |
          (int(trace_kill) << 3) | (int(trace_outcome) << 4) |
          (int(trace_replay) << 5) |
          (int(chi_out.preq.pvalid && chi_in.preq.pready) << 6) |
          (int(chi_out.preq.pbits.pallow_uretry) << 7) |
          (int(chi_in.pdat.presponse.pvalid && chi_out.pdat.presponse.pready)
           << 8) |
          (int(chi_out.prsp.prequester.pvalid && chi_in.prsp.prequester.pready)
           << 9),
      trace_address, ((chi_out.preq.pbits.paddress) & low_mask(64)));
  {
    defer(cycle, cycle + 1);
    if (reset) {
      defer(pending, 0);
      defer(beat, 0);
      defer(requests, 0);
      defer(retry_phase, 0);
      defer(retry_delay, 0);
      for (auto &count : line_requests)
        defer(count, 0);
    } else {
      if (chi_out.preq.pvalid && chi_in.preq.pready) {
        CHECK(!pending && chi_out.preq.pbits.popcode == UINT64_C(3));
        CHECK(chi_out.preq.pbits.paddress >= UINT64_C(4096) &&
              chi_out.preq.pbits.paddress < UINT64_C(6144));
        defer(line_address, ((chi_out.preq.pbits.paddress) & low_mask(64)));
        defer(transaction, chi_out.preq.pbits.ptxn_uid);
        if (chi_out.preq.pbits.pallow_uretry) {
          CHECK(line_requests[(
                    ((chi_out.preq.pbits.paddress - UINT64_C(4096)) / 64) &
                    low_mask(5))] == 0);
          defer(line_requests[(
                    ((chi_out.preq.pbits.paddress - UINT64_C(4096)) / 64) &
                    low_mask(5))],
                1);
          defer(retry_phase, 1);
          defer(retry_delay, 8);
          defer(requests, requests + 1);
        } else {
          CHECK(chi_out.preq.pbits.ppcrd_utype == 2 &&
                chi_out.preq.pbits.paddress ==
                    bit_slice(line_address, 0, (43) - (0) + 1));
          defer(pending, 1);
          defer(beat, 0);
        }
      }
      if (retry_delay > 0)
        defer(retry_delay, retry_delay - 1);
      if (chi_in.prsp.presponse.pvalid && chi_out.prsp.presponse.pready)
        defer(retry_phase, retry_phase == 1 ? 2 : 0);
      if (chi_in.pdat.presponse.pvalid && chi_out.pdat.presponse.pready) {
        if (beat == 3)
          defer(pending, 0);
        else
          defer(beat, beat + 1);
      }
    }
  }
}

void falling_update() {
  {
    fetch_trace_check(int(done));
    if (done)
      throw Finished{};
  }
}

void stimulus() {
  reset = 1;
  active = 0;
  restart = 0;
  sink_ready = 1;
  start_pc = 0;
  {
    fetch_trace_init();
    // Reset and redirect while a miss owns a backpressured/retrying transaction.
    // A redirect must not discard that owner; reset must discard it.
    start_pc = UINT64_C(4096);
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      tick();
    reset = 0;
    active = 1;
    restart = 1;
    tick();
    restart = 0;
    {
      int elapsed;
      elapsed = 0;
      while (retry_phase == 0 && elapsed < 200) {
        tick();
        elapsed++;
      }
      CHECK(retry_phase != 0);
    }
    reset = 1;
    active = 0;
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      tick();
    reset = 0;
    active = 1;
    restart = 1;
    tick();
    restart = 0;
    {
      int elapsed;
      elapsed = 0;
      while (retry_phase == 0 && elapsed < 200) {
        tick();
        elapsed++;
      }
      CHECK(retry_phase != 0);
    }
    active = 0;
    restart = 1;
    tick();
    restart = 0;
    for (unsigned repeat_index = 0; repeat_index < (100); ++repeat_index)
      tick();
    for (int mode = 0; mode < 4; mode++) {
      compressed_mode = mode == 2;
      stall_mode = mode == 3;
      sink_ready = 1;
      offset = (mode == 1 || mode == 3) ? 2 : 0;
      start_pc = UINT64_C(4096) + ((offset)&low_mask(64));
      reset = 1;
      active = 0;
      restart = 0;
      for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
        tick();
      reset = 0;
      run_stream(0);
      if (!stall_mode)
        run_stream(1);
    };
    done = 1;
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
