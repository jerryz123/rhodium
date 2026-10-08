// Preserves the rv5stage-vector-packed cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
// Checks unified packed sequencing, geometry, replay, reordered completions, and write-port contention.

bool prior_sequence = 0;

std::uint64_t prior_sequence_address;

std::uint8_t memory[8192], expected_memory[8192];

std::uint64_t bank[64], expected_bank[64];

bool outstanding[8];

std::uint64_t returns[8];

int due[8];

int cycle = 0, accepted = 0, requests = 0, writes = 0, retired_count = 0;

bool exercise_retry = 0, did_retry = 0, exercise_delay = 0, exercise_stalls = 0,
     exercise_alignment_stalls = 0;

int tests = 0, max_consecutive = 0, consecutive = 0;

int simultaneous_completions = 0;

bool all_masked = 0;

std::uint64_t memory_word(int address) {
  std::uint64_t value = {};
  for (int b = 0; b < 8; b++)
    bit_slice(value, b * 8, 8) = memory[address + b];
  return value;
}
void tick() {
  issue_ready = !exercise_stalls || cycle % 7 != 2;
  alignment_available = !exercise_alignment_stalls || cycle % 7 >= 3;
  // A fast-store VRF read reserves its next-cycle issue and align path.
  if (store && (mode == 2 || mode == 3 || (!masked && nf == 0))) {
    issue_ready = 1;
    alignment_available = 1;
  }
  write_available = !exercise_stalls || cycle % 5 >= 2;
  retry = 0;
  slow = 0;
  hit_data = 0;
  response_in = {};
  settle();
  if (!reset && attempt_out.pvalid) {
    retry = exercise_retry && !did_retry && accepted == 1;
    slow = exercise_delay && (accepted % 2 == 0);
    hit_data = memory_word(int(attempt_out.pbits.paddress));
  }
  for (int t = 0; t < 8; t++)
    if (outstanding[t] && due[t] <= cycle) {
      response_in.pvalid = 1;
      response_in.pbits.ptag = ((t)&low_mask(3));
      response_in.pbits.pdata = ((returns[t]) & low_mask(64));
    }
  settle();
  if (!reset) {
    CHECK(issued == (prior_sequence && !retry));
    if (issued)
      CHECK(token.paddress == prior_sequence_address);
    if (issued) {
      requests++;
      consecutive++;
      if (consecutive > max_consecutive)
        max_consecutive = consecutive;
      CHECK((token.paddress & ((8 - 1) & low_mask(64))) == 0 &&
            token.pmemory_uwidth == ((3) & low_mask(2)));
    } else
      consecutive = 0;
    if (response_in.pvalid)
      outstanding[int(response_in.pbits.ptag)] = 0;
    if (attempt_out.pvalid) {
      if (retry)
        did_retry = 1;
      else {
        int tag;
        tag = int(attempt_out.pbits.pcompletion_utag);
        accepted++;
        if (!slow && response_in.pvalid)
          simultaneous_completions++;
        if (store)
          for (int b = 0; b < 8; b++)
            if (bit_slice(attempt_out.pbits.pbyte_umask, b, 1))
              memory[int(attempt_out.pbits.paddress) + b] =
                  bit_slice(attempt_out.pbits.pstore_udata, b * 8, 8);
        if (slow) {
          CHECK(!outstanding[tag]);
          outstanding[tag] = 1;
          returns[tag] = hit_data;
          due[tag] = cycle + 3 + (8 - tag) * 2;
        }
      }
    }
    if (written_out.pvalid) {
      int row;
      CHECK(write_available);
      row = int(written_out.pbits.paddress);
      bank[row] = (bank[row] & ~written_out.pbits.pmask) |
                  (written_out.pbits.pdata & written_out.pbits.pmask);
      writes++;
    }
    if (retired)
      retired_count++;
  }
  prior_sequence = !reset && sequenced;
  prior_sequence_address = sequence_address;
  rising();
  falling();
  cycle++;
}

void run_case(std::uint8_t writing, int offset, int size, int fields, int start,
              std::uint8_t masking, std::uint8_t replaying,
              std::uint8_t delayed, int transfer_mode = 0, int lm = 1,
              int length = -1) {
  int count, element_bytes, register_stride, first, last, expected_beats;
  tests++;
  store = writing;
  base = ((256 + offset) & low_mask(64));
  eew = ((size)&low_mask(2));
  nf = ((fields - 1) & low_mask(3));
  if (fields > 4 && transfer_mode == 0)
    lm = 0;
  mode = ((transfer_mode)&low_mask(2));
  masked = masking;
  vtype = (((size)&low_mask(64)) << 3) | ((lm)&low_mask(64));
  vl = ((length >= 0 ? length : ((16 << lm) - 1) >> size) & low_mask(64));
  vstart = ((start)&low_mask(64));
  exercise_retry = replaying;
  did_retry = 0;
  exercise_delay = delayed;
  exercise_stalls = replaying || delayed;
  exercise_alignment_stalls = writing && (replaying || delayed);
  accepted = 0;
  requests = 0;
  writes = 0;
  retired_count = 0;
  consecutive = 0;
  max_consecutive = 0;
  for (int t = 0; t < 8; t++)
    outstanding[t] = 0;
  for (int b = 0; b < 8192; b++) {
    memory[b] = ((b * 37 + tests * 11) & low_mask(8));
    expected_memory[b] = memory[b];
  }
  for (int row = 0; row < 64; row++) {
    bank[row] = ((UINT64_C(0xb7832065ea4c9d01) ^
                  (((row)&low_mask(64)) * UINT64_C(0x1030507090b0d0f))) &
                 low_mask(64));
    if (row == 0)
      bank[row] = ((UINT64_C(0x965a3cc369a5965a)) & low_mask(64));
    if (all_masked && row < 128 / 64)
      bank[row] = 0;
    expected_bank[row] = bank[row];
    initialize_in.pvalid = 1;
    initialize_in.pbits.paddress = ((row)&low_mask(6));
    initialize_in.pbits.pdata = bank[row];
    initialize_in.pbits.pmask = UINT64_MAX;
    tick();
  }
  initialize_in = {};
  element_bytes = 1 << size;
  count = int(vl);
  register_stride = 16 << lm;
  if (transfer_mode == 2) {
    count = fields * 16 / element_bytes;
    fields = 1;
  }
  if (transfer_mode == 3) {
    count = (int(vl) + 7) / 8;
    fields = 1;
    element_bytes = 1;
  }
  for (int element = start; element < count; element++)
    for (int field = 0; field < fields; field++)
      if (!masking || transfer_mode != 0 ||
          bit_slice(bank[element / 64], element % 64, 1))
        for (int b = 0; b < element_bytes; b++) {
          int address, position, row, lane;
          address = int(base) + (element * fields + field) * element_bytes + b;
          position =
              8 * 16 + field * register_stride + element * element_bytes + b;
          row = position / 8;
          lane = position % 8;
          if (writing)
            expected_memory[address] = bit_slice(bank[row], lane * 8, 8);
          else
            bit_slice(expected_bank[row], lane * 8, 8) = memory[address];
        }
  first = int(base) + start * fields * element_bytes;
  last = int(base) + count * fields * element_bytes;
  expected_beats = start >= count ? 1 : (last + 8 - 1) / 8 - first / 8;
  request_valid = 1;
  settle();
  CHECK(request_ready);
  tick();
  request_valid = 0;
  for (int timeout = 0; timeout < 5000 && retired_count == 0; timeout++)
    tick();
  CHECK(retired_count == 1 && !active);
  CHECK(accepted == expected_beats);
  if (replaying)
    CHECK(did_retry);
  for (int row = 0; row < 64; row++)
    CHECK(bank[row] == expected_bank[row]);
  for (int b = 0; b < 8192; b++)
    CHECK(memory[b] == expected_memory[b]);
  if (!masking && fields == 1 && !delayed && !replaying &&
      expected_beats >= 3 && 8 > 2)
    CHECK(max_consecutive >= 3);
  tick();
}

void drive() {}

void observe() {}

void falling_update() {}

void stimulus() {
  reset = 1;
  request_valid = 0;
  issue_ready = 1;
  retry = 0;
  slow = 0;
  alignment_available = 1;
  write_available = 1;
  hit_data = 0;
  {
    initialize_in = {};
    response_in = {};
    base = 0;
    vl = 0;
    vstart = 0;
    vtype = 0;
    nf = 0;
    eew = 0;
    mode = 0;
    store = 0;
    masked = 0;
    tick();
    tick();
    reset = 0;
    tick();
    for (int size = 0; size <= 3; size++)
      for (int offset = 0; offset < 8; offset += (1 << size))
        for (int writing = 0; writing < 2; writing++) {
          run_case(((writing)&low_mask(1)), offset, size, 1, 0, 0, 0, 0);
          run_case(((writing)&low_mask(1)), offset, size, 1, 1, 1, 1, 1);
          for (int fields = 2; fields <= 8; fields++)
            run_case(((writing)&low_mask(1)), offset, size, fields,
                     size == 3 && fields > 4 ? 0 : 1,
                     ((fields % 2) & low_mask(1)), 1, 1);
        }
    for (int offset = 0; offset < 8; offset++)
      for (int writing = 0; writing < 2; writing++) {
        run_case(((writing)&low_mask(1)), offset, 0, 8, 1, 0, 1, 1, 2);
        run_case(((writing)&low_mask(1)), offset, 0, 1, 0, 0, 0, 1, 3);
      }
    run_case(0, 3, 0, 1, 61, 1, 1, 1, 0, 3, 121);
    run_case(1, 5, 0, 1, 61, 1, 1, 1, 0, 3, 121);
    for (int writing = 0; writing < 2; writing++) {
      run_case(((writing)&low_mask(1)), 3, 0, 1, 0, 0, 0, 0, 0, 1, 0);
      run_case(((writing)&low_mask(1)), 3, 0, 1, 7, 0, 0, 0, 0, 1, 7);
      run_case(((writing)&low_mask(1)), 3, 0, 1, 256, 0, 0, 0);
      run_case(((writing)&low_mask(1)), 3, 0, 8, 256, 0, 0, 0, 2);
      run_case(((writing)&low_mask(1)), 3, 0, 1, 256, 0, 0, 0, 3);
      all_masked = 1;
      run_case(((writing)&low_mask(1)), 3, 0, 1, 0, 1, 1, 1);
      all_masked = 0;
    }
    if (8 > 2)
      CHECK(simultaneous_completions > 0);

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
