// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <random>
#include <set>
#include <vector>

static void check_issue() {
  eval();
  unsigned expected = 0, extra_reads = 0, memories = 0, branches = 0, deferred = 0;
  bool serialized = false;
  std::set<unsigned> written, deferred_written, normal_written;
  unsigned floating_address = 0;
  for (unsigned lane = 0; lane < 3; ++lane) {
    const auto &c = candidates[lane];
    std::set<unsigned> reads;
    if (c.puses_urs1 && c.prs1) reads.insert(c.prs1);
    if (c.puses_urs2 && c.prs2) reads.insert(c.prs2);
    const bool extra = reads.size() == 2;
    bool hazard = false;
    for (unsigned r : reads) hazard |= written.contains(r);
    const bool writes = c.pwrite && c.prd;
    hazard |= writes && (deferred_written.contains(c.prd) || (c.pdeferred && written.contains(c.prd)));
    auto next_normal = normal_written;
    if (writes && c.pnormal_uwrite) next_normal.insert(c.prd);
    if (!c.pvalid || !c.pready || hazard || serialized ||
        (c.pserializing && (lane != 0 || !drained)) ||
        extra_reads + extra > 1 || memories + c.pmemory > 1 ||
        branches + c.pbranch > 1 || deferred + c.pdeferred > 1 ||
        next_normal.size() > normal_write_ports) break;
    ++expected;
    extra_reads += extra;
    memories += c.pmemory;
    branches += c.pbranch;
    deferred += c.pdeferred;
    serialized |= c.pserializing;
    normal_written = next_normal;
    if (extra) floating_address = c.prs2;
    if (writes) written.insert(c.prd);
    if (writes && c.pdeferred) deferred_written.insert(c.prd);
  }
  CHECK(count == expected);
  CHECK(read_addresses[3] == floating_address);
  for (unsigned lane = 0; lane < 3; ++lane) {
    const auto &c = candidates[lane];
    const bool first = c.puses_urs1 && c.prs1;
    const bool second = c.puses_urs2 && c.prs2;
    CHECK(read_addresses[lane] == (first ? c.prs1 : second ? c.prs2 : 0));
    CHECK(first_is_rs2[lane] == (!first && second));
    CHECK(second_read[lane] == (first && second && c.prs1 != c.prs2));
  }
}

static void check_writes() {
  std::vector<unsigned> winners;
  for (unsigned i = 0; i < 3; ++i) {
    bool keep = retiring_valid[i] && retiring_data[i].paddress;
    for (unsigned j = i + 1; j < 3; ++j)
      if (retiring_valid[j] && retiring_data[j].paddress == retiring_data[i].paddress) keep = false;
    if (keep) winners.push_back(i);
  }
  if (winners.size() + completion_valid > 2) return;
  eval();
  for (unsigned i = 0; i < 3; ++i)
    CHECK(surviving[i] == (std::find(winners.begin(), winners.end(), i) != winners.end()));
  CHECK(write_valid[0] == !winners.empty());
  CHECK(write_valid[1] == (completion_valid || winners.size() == 2));
  if (!winners.empty()) {
    CHECK(write_data[0].paddress == retiring_data[winners[0]].paddress);
    CHECK(write_data[0].pdata == retiring_data[winners[0]].pdata);
  }
  if (write_valid[1]) {
    const auto &value = completion_valid ? completion_data : retiring_data[winners[1]];
    CHECK(write_data[1].paddress == value.paddress);
    CHECK(write_data[1].pdata == value.pdata);
  }
  tick_model();
}

int main() {
  return run_test([] {
    reset = 1;
    tick_model();
    reset = 0;
    normal_write_ports = 2;
    drained = 1;
    candidates = {};
    for (auto &c : candidates) { c.pvalid = 1; c.pready = 1; c.pwrite = 1; c.pnormal_uwrite = 1; }
    candidates[0].prd = 5;
    candidates[1].prd = 6;
    candidates[2].prd = 5;
    check_issue();
    CHECK(count == 3);
    candidates[2].prd = 7;
    check_issue();
    CHECK(count == 2);

    for (unsigned destinations = 0; destinations < 64; ++destinations)
      for (unsigned deferred_mask = 0; deferred_mask < 8; ++deferred_mask)
        for (unsigned ports = 0; ports <= 2; ++ports) {
          normal_write_ports = ports;
          for (unsigned i = 0; i < 3; ++i) {
            candidates[i].prd = (destinations >> (2 * i)) & 3;
            candidates[i].pdeferred = (deferred_mask >> i) & 1;
          }
          check_issue();
        }

    // Independent source-set and resource oracle, including unavailable oldest owners.
    std::mt19937 random(0x3a4b2c);
    for (unsigned sample = 0; sample < 30000; ++sample) {
      normal_write_ports = random() % 3;
      drained = random() & 1;
      for (auto &c : candidates) {
        c.pvalid = random() % 8 != 0;
        c.pready = random() % 8 != 0;
        c.prs1 = random() % 8; c.prs2 = random() % 8;
        c.puses_urs1 = random() & 1; c.puses_urs2 = random() & 1;
        c.prd = random() % 8; c.pwrite = random() & 1;
        c.pnormal_uwrite = random() & 1;
        c.pdeferred = random() % 8 == 0; c.pmemory = random() % 4 == 0;
        c.pbranch = random() % 4 == 0; c.pserializing = random() % 16 == 0;
      }
      check_issue();
    }
    candidates = {};
    completion_data = {31, 999};
    // Every successful subset: a faulting/replayed younger writer never masks an older write.
    for (unsigned dest = 0; dest < 64; ++dest)
      for (unsigned success = 0; success < 8; ++success)
        for (unsigned delayed = 0; delayed < 2; ++delayed) {
          for (unsigned i = 0; i < 3; ++i) {
            retiring_data[i] = {std::uint8_t((dest >> (2 * i)) & 3), 100 + i};
            retiring_valid[i] = (success >> i) & 1;
          }
          completion_valid = delayed;
          check_writes();
        }
    completion_valid = 0;
    retiring_valid = {1, 1, 1};
    retiring_data = {{{1, 10}, {2, 20}, {3, 30}}};
    expect_failure("rv3wide_writeback_capacity", [] { tick_model(); });
  });
}
