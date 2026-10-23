// Preserves the rv5stage-vector-packed-rv32 cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "../../../tests/rsim/driver.hpp"
#include "packed-lineage.hpp"
// Checks four-byte packed transport into native four-byte VRF rows.
// Checks unified packed sequencing, geometry, replay, reordered completions, and write-port contention.

bool prior_sequence = 0;

std::uint32_t prior_sequence_address;

std::uint8_t memory[8192], expected_memory[8192];

std::uint32_t bank[128], expected_bank[128];

bool outstanding[8];
bool direct_rows[8];

std::uint64_t returns[8];

int due[8];

int cycle = 0, accepted = 0, requests = 0, writes = 0, retired_count = 0;

bool exercise_retry = 0, did_retry = 0, exercise_delay = 0, exercise_stalls = 0,
     exercise_alignment_stalls = 0;

int tests = 0, max_consecutive = 0, consecutive = 0;

int simultaneous_completions = 0;
int direct_hits = 0, direct_hits_past_pending = 0, direct_hits_without_alignment = 0;
int release_cursor = 0, direct_head_hits = 0, direct_head_returns = 0,
    direct_younger_hits = 0;

bool all_masked = 0;
bool hold_write_port = false;

std::uint64_t memory_word(int address) {
  std::uint64_t value = {};
  for (int b = 0; b < 4; b++)
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
  write_available = !hold_write_port && (!exercise_stalls || cycle % 5 >= 2);
  retry = 0;
  slow = 0;
  hit_data = 0;
  response_in = {};
  settle();
  if (!reset && attempt_out.pvalid) {
    retry = exercise_retry && !did_retry && accepted == 1;
    // Hits require a promised write cycle; delayed returns remain backpressurable.
    slow = (exercise_delay && (accepted % 2 == 0)) || (!store && !write_available);
    hit_data = memory_word(int(attempt_out.pbits.paddress));
  }
  for (int t = 0; t < 8; t++)
    if (outstanding[t] && due[t] <= cycle) {
      response_in.pvalid = 1;
      response_in.pbits.ptag = ((t)&low_mask(3));
      response_in.pbits.pdata = ((returns[t]) & low_mask(32));
    }
  settle();
  packed_lineage::sample(reset, 4, attempt_out.pvalid,
      attempt_out.pbits.pcompletion_utag, attempt_out.pbits.paddress,
      attempt_out.pbits.pbyte_umask, retry, store, written_out.pvalid,
      written_out.pbits.paddress, written_out.pbits.pmask);
  if (!reset) {
    CHECK(issued == (prior_sequence && !retry));
    if (issued)
      CHECK(token.paddress == prior_sequence_address);
    if (issued) {
      requests++;
      consecutive++;
      if (consecutive > max_consecutive)
        max_consecutive = consecutive;
      CHECK((token.paddress & ((4 - 1) & low_mask(32))) == 0 &&
            token.pmemory_uwidth == ((2) & low_mask(2)));
    } else
      consecutive = 0;
    if (response_in.pvalid && response_out.pready) {
      const int tag = int(response_in.pbits.ptag);
      if (direct_rows[tag] && tag == release_cursor) {
        CHECK(written_out.pvalid && released_out.pvalid &&
              released_out.pbits == tag);
        ++direct_head_returns;
      }
      outstanding[int(response_in.pbits.ptag)] = 0;
    }
    if (attempt_out.pvalid) {
      if (retry)
        did_retry = 1;
      else {
        int tag;
        tag = int(attempt_out.pbits.pcompletion_utag);
        direct_rows[tag] = !store && !masked && nf == 0 && mode == 0 &&
                           vstart == 0 && (base % 4) == 0 &&
                           attempt_out.pbits.pbyte_umask == 15;
        accepted++;
        if (!slow && response_in.pvalid && response_out.pready)
          simultaneous_completions++;
        if (!store && !slow && !masked && nf == 0 && mode == 0 &&
            (base % 4) == 0 && attempt_out.pbits.pbyte_umask == 15) {
          CHECK(written_out.pvalid);
          CHECK(written_out.pbits.paddress ==
                (8 * 16 + attempt_out.pbits.paddress - base) / 4);
          CHECK(written_out.pbits.pdata == hit_data &&
                written_out.pbits.pmask == UINT32_MAX);
          ++direct_hits;
          for (int older = 0; older < 8; ++older)
            if (outstanding[older]) { ++direct_hits_past_pending; break; }
          direct_hits_without_alignment += !alignment_available;
          if (direct_rows[tag] && tag == release_cursor) {
            CHECK(released_out.pvalid && released_out.pbits == tag);
            ++direct_head_hits;
          } else if (tag != release_cursor) {
            CHECK(!released_out.pvalid || released_out.pbits != tag);
            ++direct_younger_hits;
          }
        }
        if (store)
          for (int b = 0; b < 4; b++)
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
    if (released_out.pvalid) {
      CHECK(released_out.pbits == release_cursor);
      release_cursor = (release_cursor + 1) % 8;
    }
  }
  prior_sequence = !reset && sequenced;
  prior_sequence_address = sequence_address;
  rising();
  falling();
  packed_lineage::check();
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
  exercise_alignment_stalls = replaying || delayed;
  accepted = 0;
  requests = 0;
  writes = 0;
  retired_count = 0;
  consecutive = 0;
  max_consecutive = 0;
  for (int t = 0; t < 8; t++) {
    outstanding[t] = 0;
    direct_rows[t] = 0;
  }
  for (int b = 0; b < 8192; b++) {
    memory[b] = ((b * 37 + tests * 11) & low_mask(8));
    expected_memory[b] = memory[b];
  }
  for (int row = 0; row < 128; row++) {
    bank[row] = ((UINT64_C(0xb7832065ea4c9d01) ^
                  (((row)&low_mask(64)) * UINT64_C(0x1030507090b0d0f))) &
                 low_mask(32));
    if (row == 0)
      bank[row] = ((UINT64_C(0x965a3cc369a5965a)) & low_mask(32));
    if (all_masked && row < 128 / 32)
      bank[row] = 0;
    expected_bank[row] = bank[row];
    initialize_in.pvalid = 1;
    initialize_in.pbits.paddress = ((row)&low_mask(7));
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
  packed_lineage::begin_case();
  for (int element = start; element < count; element++)
    for (int field = 0; field < fields; field++)
      if (!masking || transfer_mode != 0 ||
          bit_slice(bank[element / 32], element % 32, 1))
        for (int b = 0; b < element_bytes; b++) {
          int address, position, row, lane;
          address = int(base) + (element * fields + field) * element_bytes + b;
          position =
              8 * 16 + field * register_stride + element * element_bytes + b;
          row = position / 4;
          lane = position % 4;
          if (writing)
            expected_memory[address] = bit_slice(bank[row], lane * 8, 8);
          else {
            bit_slice(expected_bank[row], lane * 8, 8) = memory[address];
            CHECK(packed_lineage::layout.emplace(address, position).second);
          }
        }
  first = int(base) + start * fields * element_bytes;
  last = int(base) + count * fields * element_bytes;
  expected_beats = start >= count ? 1 : (last + 4 - 1) / 4 - first / 4;
  request_valid = 1;
  settle();
  CHECK(request_ready);
  tick();
  request_valid = 0;
  for (int timeout = 0; timeout < 5000 && retired_count == 0; timeout++)
    tick();
  CHECK(retired_count == 1 && !active);
  CHECK(accepted == expected_beats);
  CHECK(packed_lineage::owners.empty());
  for (bool pending : outstanding)
    CHECK(!pending);
  if (writing)
    CHECK(writes == 0);
  if (replaying)
    CHECK(did_retry);
  for (int row = 0; row < 128; row++)
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
    // Store acknowledgements reclaim ordered slots even while compute owns every VRF write cycle.
    hold_write_port = true;
    run_case(1, 0, 0, 1, 0, 0, 0, 0, 0, 3, 128); // wrap the eight-slot ring
    run_case(1, 3, 0, 4, 1, 1, 1, 1); // masked segments, retry, and reordered responses
    run_case(1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0); // empty store
    hold_write_port = false;
    run_case(0, 0, 0, 1, 0, 0, 0, 0, 0, 3, 128); // same-edge writes/releases across ring wrap
    run_case(0, 0, 0, 1, 0, 0, 0, 1, 0, 3, 128);
    for (int size = 0; size <= 2; size++)
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
    CHECK(direct_hits > 0 && direct_hits_past_pending > 0 &&
          direct_hits_without_alignment > 0);
    CHECK(direct_head_hits > 8 && direct_head_returns > 0 && direct_younger_hits > 0);

    throw Finished{};
  }
}

int main() {
  return run_test([] {
    rheg::graph().bind_manifest(rheg_generated::manifest());
    cycle_limit = 1000000;
    try {
      stimulus();
      for (;;)
        rising();
    } catch (const Finished &) {
    }
    packed_lineage::finish();
  });
}
