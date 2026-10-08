// SPDX-License-Identifier: Apache-2.0
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
std::uint16_t memory[4][2]{};
std::uint32_t expected_response{};
bool expected_valid = 0, swept = 0, prefer_commit = 0, write_op, read_op;
int sweep_set = 0, priority_slot = 0, grants[3]{}, reads = 0, winner;
unsigned expected_grant{};

void step() {
  eval();
  winner = -1;
  for (int offset = 0; offset < 3; offset++)
    if (winner < 0 && ((commit_valid >> ((priority_slot + offset) % 3)) & 1))
      winner = (priority_slot + offset) % 3;
  write_op = swept && !reset && winner >= 0 && (!lookup_valid || prefer_commit);
  read_op = swept && !reset && lookup_valid && !write_op;
  expected_grant = write_op ? ((1 << winner) & low_mask(3)) : 0;
  CHECK(initialized == (swept && !reset) && lookup_accepted == read_op &&
        commit_accepted == expected_grant);
  CHECK(response_valid == (expected_valid && swept && !reset));
  if (response_valid)
    CHECK(response == expected_response);
  if (reset) {
    swept = 0;
    sweep_set = 0;
    priority_slot = 0;
    prefer_commit = 0;
    expected_valid = 0;
  } else if (!swept) {
    memory[sweep_set][0] = 0;
    memory[sweep_set][1] = 0;
    if (sweep_set == 3)
      swept = 1;
    else
      sweep_set++;
    expected_valid = 0;
  } else {
    expected_valid = read_op;
    if (read_op) {
      expected_response =
          (std::uint32_t(memory[lookup_set][1]) << 16) | memory[lookup_set][0];
      prefer_commit = 1;
      reads++;
    }
    if (write_op) {
      memory[(commit_sets >> (2 * winner)) & 3][(commit_ways >> winner) & 1] =
          (commit_entries >> (16 * winner)) & 65535;
      priority_slot = (winner + 1) % 3;
      prefer_commit = 0;
      grants[winner]++;
    }
  }
  tick_model();
}

int main() {
  return run_test([] {
    reset = 1;
    lookup_valid = 0;
    step();
    reset = 0;
    commit_valid = 7;
    lookup_valid = 1;
    step();
    step();
    reset = 1;
    step();
    reset = 0;
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      step();

    commit_sets = 0x24;
    commit_ways = UINT64_C(2);
    commit_entries = UINT64_C(170133695238708);
    for (int i = 0; i < 60; i++) {
      lookup_set = ((i)&low_mask(2));
      step();
    }
    CHECK(reads == 30 && grants[0] == 10 && grants[1] == 10 && grants[2] == 10);

    for (int i = 0; i < 100; i++) {
      commit_valid = ((i)&low_mask(3));
      lookup_valid = (i % 3 != 0);
      lookup_set = ((i / 3) & low_mask(2));
      commit_sets = ((i)&low_mask(6));
      commit_ways = ((i / 7) & low_mask(3));
      commit_entries = UINT64_C(20015998343868) ^ ((i * 65539) & low_mask(48));
      step();
    }
    commit_valid = 0;
    lookup_valid = 1;
    for (int i = 0; i < 4; i++) {
      lookup_set = ((i)&low_mask(2));
      step();
    }

    commit_valid = 7;
    reset = 1;
    step();
    reset = 0;
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      step();
    commit_valid = 0;
    for (int i = 0; i < 4; i++) {
      lookup_set = ((i)&low_mask(2));
      step();
    }
    lookup_valid = 0;
    step();
  });
}
