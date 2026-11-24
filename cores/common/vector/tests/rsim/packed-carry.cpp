// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

using Word = decltype(commit_in.pbits.pdata);
constexpr unsigned bytes = sizeof(Word), slots = 4;
constexpr unsigned all_bytes = (1u << bytes) - 1;
std::array<Word, slots> slot_words{};
unsigned writes = 0, releases = 0;

Word byte_mask(unsigned enabled) {
  Word result = 0;
  for (unsigned byte = 0; byte < bytes; ++byte)
    if (enabled & (1u << byte)) result |= Word(0xff) << (byte * 8);
  return result;
}

Word pattern(unsigned seed) {
  Word result = 0;
  for (unsigned byte = 0; byte < bytes; ++byte)
    result |= Word((seed + 37 * byte) & 0xff) << (byte * 8);
  return result;
}

void settle() {
  head_word = slot_words[head];
  eval();
  // Independent byte rotation supplies the shared alignment service.
  const unsigned shift = alignment_out.pbits.pbytes;
  const Word data = alignment_out.pbits.pdata;
  aligned = shift ? Word((data >> (shift * 8)) |
                         (data << ((bytes - shift) * 8))) : data;
  eval();
}

struct Expected {
  bool write = false;
  unsigned row = 0;
  Word data = 0;
  unsigned mask = 0;
  bool release = false;
};

void step(Expected expected = {}) {
  settle();
  CHECK(bool(write_out.pvalid) == expected.write);
  CHECK(bool(released_out.pvalid) == expected.release);
  if (expected.write) {
    CHECK(write_out.pbits.paddress == expected.row);
    CHECK(write_out.pbits.pmask == byte_mask(expected.mask));
    CHECK((write_out.pbits.pdata & byte_mask(expected.mask)) ==
          (expected.data & byte_mask(expected.mask)));
    ++writes;
  }
  if (expected.release) {
    CHECK(released_out.pbits == head);
    ++releases;
  }
  if (retain_hit_out.pvalid)
    slot_words[retain_hit_out.pbits.ptag] = retain_hit_out.pbits.pdata;
  if (retain_return_out.pvalid)
    slot_words[retain_return_out.pbits.ptag] = retain_return_out.pbits.pdata;
  auto accepted = accepted_slots;
  if (commit_in.pvalid) accepted[commit_in.pbits.ptag] = 1;
  if (expected.release) accepted[head] = 0;
  const unsigned next_head = expected.release ? (head + 1) % slots : unsigned(head);
  tick_model();
  accepted_slots = accepted;
  head = next_head;
}

void reserve_row(unsigned row, unsigned enabled, unsigned instruction_owner,
                 unsigned split = 0) {
  CHECK(!accepted_slots[head]);
  reserve_in = {};
  reserve_in.pvalid = 1;
  reserve_in.pbits.ptag = head;
  auto &entry = reserve_in.pbits.pentry;
  entry.pmask = enabled;
  entry.powner = instruction_owner;
  entry.playout.plive = enabled;
  for (unsigned lane = 0; lane < bytes; ++lane) {
    entry.playout.prows[lane] = row + (split && lane >= split);
    entry.playout.poffsets[lane] = split ? (bytes - split + lane) % bytes : lane;
  }
  step();
  reserve_in.pvalid = 0;
}

void accept(Word data, bool delayed = false) {
  commit_in = {};
  commit_in.pvalid = 1;
  commit_in.pbits.ptag = head;
  commit_in.pbits.pdata = data;
  commit_in.pbits.pslow = delayed;
  step();
  commit_in.pvalid = 0;
}

void claim(unsigned row, unsigned query_owner, bool foreign) {
  owner = query_owner;
  settle();
  CHECK(active);
  for (unsigned index = 0; index < busy_rows.size(); ++index) {
    CHECK(bool(busy_rows[index]) == (index == row));
    CHECK(bool(foreign_rows[index]) == (foreign && index == row));
  }
}

void handoff(unsigned old_owner, unsigned row) {
  const unsigned young_owner = old_owner ^ 1;
  const unsigned old_mask = (1u << 1) | (1u << (bytes - 1));
  const unsigned young_mask = 1u | (1u << 1);
  const Word old_data = pattern(21 + row), young_data = pattern(167 + row);
  const unsigned start_writes = writes, start_releases = releases;
  closed_owners = {};
  owner = old_owner;
  write_available = 0;
  reserve_row(row, old_mask, old_owner);
  accept(old_data, true);
  completion_in.pvalid = 1;
  completion_in.pbits.ptag = head;
  completion_in.pbits.pdata = old_data;
  settle();
  CHECK(completion_out.pready);
  step();
  completion_in.pvalid = 0;
  for (unsigned cycle = 0; cycle < 4; ++cycle) step();
  write_available = 1;
  step({false, 0, 0, 0, true}); // Capture a partial row and reclaim its slot.
  claim(row, old_owner, false);
  claim(row, young_owner, true);
  closed_owners[old_owner] = 1;
  write_available = 0;
  // Accept the next owner's overlapping row while the old suffix is blocked.
  // Its slot may retain a result, but it cannot merge into the older carry.
  reserve_row(row, young_mask, young_owner);
  accept(young_data);
  for (unsigned cycle = 0; cycle < 5; ++cycle) {
    // Live younger payloads must not replace the accepted old carry.
    commit_in.pbits.pdata = pattern(cycle + 200);
    reserve_in.pbits.pentry.powner = young_owner;
    settle();
    CHECK(drain_blocked);
    CHECK(!alignment_out.pvalid);
    claim(row, young_owner, true);
    step();
  }
  write_available = 1;
  step({true, row, old_data, old_mask, false});
  settle();
  CHECK(active && busy_rows[row] && !foreign_rows[row]);

  // Reuse the same row under the next owner, with an overlapping byte and a
  // different mask. Bytes from the prior owner must not leak into this write.
  step({false, 0, 0, 0, true}); // Capture the successor; no duplicate old flush.
  claim(row, young_owner, false);
  claim(row, old_owner, true);
  closed_owners[young_owner] = 1;
  write_available = 0;
  for (unsigned cycle = 0; cycle < 3; ++cycle) step();
  write_available = 1;
  step({true, row, young_data, young_mask, false});
  step();
  settle();
  CHECK(!active && !busy_rows[row]);
  CHECK(writes == start_writes + 2 && releases == start_releases + 2);
}

void split_merge(unsigned count, unsigned row) {
  closed_owners = {};
  owner = count % 2;
  const Word first = pattern(count + 7), second = pattern(count + 93);
  const unsigned prefix_mask = ((1u << count) - 1) << (bytes - count);
  const unsigned suffix_mask = (1u << (bytes - count)) - 1;
  write_available = 0;
  reserve_row(row, all_bytes, owner, count);
  accept(first);
  for (unsigned cycle = 0; cycle < 3; ++cycle) step();
  write_available = 1;
  step({true, row, Word(first << ((bytes - count) * 8)), prefix_mask, true});
  claim(row + 1, owner, false);
  settle();
  CHECK(!busy_rows[row]);

  // The next beat completes the carried suffix. Stall before its read and
  // ensure a full row writes and releases on the same edge once granted.
  write_available = 0;
  reserve_row(row + 1, all_bytes ^ suffix_mask, owner);
  accept(second);
  for (unsigned cycle = 0; cycle < 4; ++cycle) step();
  write_available = 1;
  const Word combined = (first >> (count * 8)) |
                        (second & byte_mask(all_bytes ^ suffix_mask));
  step({true, row + 1, combined, all_bytes, true});
  step();
  settle();
  CHECK(!active && !busy_rows[row + 1]);
}

int main() {
  return run_test([] {
    reset = 1;
    tick_model();
    reset = 0;
    drain_enable = alignment_available = direct_write_available = 1;
    for (unsigned repeat = 0; repeat < 8; ++repeat)
      handoff(repeat % 2, 8 + repeat);
    for (unsigned count = 1; count < bytes; ++count)
      split_merge(count, 24 + count * 2);
  });
}
