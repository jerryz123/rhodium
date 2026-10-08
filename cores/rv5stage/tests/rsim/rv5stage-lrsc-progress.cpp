// Preserves the rv5stage-lrsc-progress cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"

// Exercises real L1Ds and inclusive LLC eviction against LR/SC and competing writes.

auto &core_in(unsigned i) {
  CHECK(i < 2);
  return i ? core_1_in : core_0_in;
}

const auto &core_out(unsigned i) {
  CHECK(i < 2);
  return i ? core_1_out : core_0_out;
}

int completions[2] = {0, 0};

std::uint64_t results[2];

void tick() {
  for (int client = 0; client < 2; client++) {
    if (!reset && core_out(client).presponse.pvalid) {
      CHECK(!core_out(client).presponse.pbits.paccess_ufault);
      completions[client]++;
      results[client] = core_out(client).presponse.pbits.pdata;
    }
  }
  rising();
  settle();
}

void issue(int client, std::uint8_t operation, std::uint64_t address,
           std::uint64_t data = 0) {
  for (int cycle = 0; !core_out(client).prequest.pready && cycle < 500; cycle++)
    tick();
  CHECK(core_out(client).prequest.pready);
  core_in(client).prequest.pbits = {};
  core_in(client).prequest.pbits.paddress = address;
  core_in(client).prequest.pbits.paccess = operation;
  core_in(client).prequest.pbits.pwidth = 3;
  core_in(client).prequest.pbits.pbyte_umask = UINT64_C(255);
  core_in(client).prequest.pbits.pdata = data;
  core_in(client).prequest.pbits.pcontext.pwriteback =
      operation == 2 ? UINT64_C(0) : UINT64_C(128);
  core_in(client).prequest.pvalid = 1;
  tick();
  core_in(client).prequest.pvalid = 0;
}

void await_count(int client, int target, std::uint64_t value,
                 std::uint8_t check_value = 1) {
  for (int cycle = 0; completions[client] < target && cycle < 1000; cycle++)
    tick();
  CHECK(completions[client] == target &&
        (!check_value || results[client] == value));
}

void drive() {}

void observe() {}

void falling_update() {}

void stimulus() {
  reset = 1;
  {
    int left_count, right_count, first, second;
    int baseline[2];
    core_in(0) = {};
    core_in(1) = {};
    core_in(0).presponse.pready = 1;
    core_in(1).presponse.pready = 1;
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick();
    reset = 0;
    tick();
    // Initialize the architectural word through the coherent path, not SRAM
    // hierarchy. Other words are read only to cause replacement; their initial
    // data is deliberately not asserted.
    issue(0, 2, 0, 1);
    await_count(0, 1, 0);
    for (int iteration = 0; iteration < 12; iteration++) {
      left_count = completions[0];
      right_count = completions[1];
      issue(0, 3, 0);
      await_count(0, left_count + 1, ((iteration + 1) & low_mask(64)));
      CHECK(core_out(0).preservation_uvalid);
      // All addresses conflict in the one-way LLC, including a line still
      // resident in L1D. The other client only reads: no external write can
      // excuse failure to make progress on the reserved word.
      issue(1, 1, ((128 + iteration * 128) & low_mask(64)));
      for (int cycle = 0; !eviction_waiting && cycle < 60; cycle++)
        tick();
      CHECK(eviction_waiting);
      for (int repeat_index = 0; repeat_index < (40); ++repeat_index) {
        CHECK(eviction_waiting && core_out(0).preservation_uvalid);
        tick();
      }
      issue(0, 4, 0, ((iteration + 2) & low_mask(64)));
      await_count(0, left_count + 2, 0);
      await_count(1, right_count + 1, 0, 0);
    }
    // The last dirty SC value must survive LLC eviction and SRAM writeback.
    left_count = completions[0];
    issue(0, 1, 0);
    await_count(0, left_count + 1, 13);

    // A stalled reserving client must eventually let a conflicting writer
    // acquire the line. Its subsequently revoked SC must not overwrite it.
    left_count = completions[0];
    right_count = completions[1];
    issue(0, 3, 0);
    await_count(0, left_count + 1, 13);
    issue(1, 2, 0, 99);
    await_count(1, right_count + 1, 0);
    CHECK(!core_out(0).preservation_uvalid);
    issue(0, 4, 0, 77);
    await_count(0, left_count + 2, 1);
    issue(0, 1, 0);
    await_count(0, left_count + 3, 99);
    // Simultaneous contenders must not both reserve a shared copy and then
    // deadlock upgrading at SC. Whichever obtains LR ownership first can
    // complete its local SC while the other acquisition waits at Home.
    baseline[0] = completions[0];
    baseline[1] = completions[1];
    issue(0, 3, 0);
    issue(1, 3, 0);
    for (int cycle = 0; completions[0] == baseline[0] &&
                        completions[1] == baseline[1] && cycle < 1000;
         cycle++)
      tick();
    CHECK(completions[0] != baseline[0] || completions[1] != baseline[1]);
    first = completions[0] != baseline[0] ? 0 : 1;
    second = 1 - first;
    await_count(first, baseline[first] + 1, 99);
    issue(first, 4, 0, 100);
    await_count(first, baseline[first] + 2, 0);
    await_count(second, baseline[second] + 1, 100);
    issue(second, 4, 0, 101);
    await_count(second, baseline[second] + 2, 0);
    left_count = completions[0];
    issue(0, 1, 0);
    await_count(0, left_count + 1, 101);

    throw Finished{};
  }
}

int main() {
  return run_test([] {
    cycle_limit = 200002;
    try {
      stimulus();
      for (;;)
        rising();
    } catch (const Finished &) {
    }
  });
}
