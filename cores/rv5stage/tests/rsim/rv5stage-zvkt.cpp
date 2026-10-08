// Preserves the rv5stage-zvkt cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
// Compares vector timing under identical control and distinct data for every implemented Zvkt form.

int cycles, commits, differing_results;

std::uint64_t rng = UINT64_C(0xd1b54a32d192ed03);

std::uint64_t random_word() {
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return rng;
}

void tick() {
  falling();
  settle();
  cycles++;
  if (committed)
    commits++;
  if (data_differed)
    differing_results++;
}

void initialize_banks() {
  std::uint64_t left_data, right_data;
  for (int address = 0; address < 32 * 128 / 64; address++) {
    left_data = UINT64_C(0x101010101010101) * ((address + 1) & low_mask(64));
    right_data = random_word();
    // Ordinary v0 masks are control; carry/merge consume v0 as data instead.
    if (address < 128 / 64 ||
        (control_vs1 && address >= 16 * 128 / 64 && address < 17 * 128 / 64)) {
      left_data = address < 128 / 64 ? UINT64_C(0xd6b59a6cc3a55aa5)
                                     : UINT64_C(0x1000000030002);
      right_data = address < 128 / 64 && v0_data ? ~left_data : left_data;
    }
    left_initialize_in = {
        UINT64_C(1),
        {std::uint8_t(address & 63), left_data, UINT64_C(0xffffffffffffffff)}};
    right_initialize_in = {
        UINT64_C(1),
        {std::uint8_t(address & 63), right_data, UINT64_C(0xffffffffffffffff)}};
    tick();
  }
  left_initialize_in.pvalid = 0;
  right_initialize_in.pvalid = 0;
}

void run_probe(int index, int start) {
  int elapsed, prior_commits;
  probe = ((index)&low_mask(9));
  settle();
  vtype = (((sew64 ? 3 : 2) << 3) & low_mask(64));
  vl = sew64 ? 2 : 3;
  vstart = ((start)&low_mask(64));
  left_scalar = control_rs1 ? 3 : UINT64_C(0x123456789abcdef);
  right_scalar = control_rs1 ? 3 : UINT64_C(0xfedcba9876543210);
  left_floating_scalar = UINT64_C(0xffffffff3f800000);
  right_floating_scalar = UINT64_C(0xffffffffc0200000);
  vxrm = ((index)&low_mask(2));
  request_valid = 0;
  issue_ready = 1;
  cancel = 0;
  reset = 1;
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
    tick();
  reset = 0;
  initialize_banks();
  CHECK(legal);
  prior_commits = commits;
  request_valid = 1;
  while (!request_ready)
    tick();
  tick();
  request_valid = 0;
  for (elapsed = 0; elapsed < 500 && (active || commits == prior_commits);
       elapsed++) {
    issue_ready = (elapsed + index) % 5 != 0;
    tick();
  }
  issue_ready = 1;
  CHECK(!active && commits > prior_commits);
}

void drive() {}

void observe() {}

void falling_update() {}

void stimulus() {
  reset = 1;
  {
    probe = 0;
    vtype = 0;
    vl = 0;
    vstart = 0;
    left_scalar = 0;
    right_scalar = 0;
    left_floating_scalar = 0;
    right_floating_scalar = 0;
    vxrm = 0;
    request_valid = 0;
    issue_ready = 0;
    cancel = 0;
    left_initialize_in = {};
    right_initialize_in = {};
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      tick();
    for (int index = 0; index < int(probe_count); index++) {
      run_probe(index, 0);
      run_probe(index, 1);
    }
    CHECK(differing_results > 0);

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
