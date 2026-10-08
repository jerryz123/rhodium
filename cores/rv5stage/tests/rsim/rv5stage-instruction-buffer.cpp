// Preserves the rv5stage-instruction-buffer cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
// Checks zero-cycle Decode bypass, parcel retention, stalls, precise faults, and clear priority.

void offer(std::uint64_t pc, std::uint32_t word,
           std::uint8_t mask = UINT64_C(3)) {
  falling();
  packets_in = {UINT64_C(1), {pc, word, mask, UINT64_C(0), UINT64_C(0), {}}};
  settle();
}
void expect_instruction(std::uint64_t pc, std::uint32_t raw) {
  CHECK(fetched_out.pvalid && fetched_out.pbits.ppc == pc &&
        fetched_out.pbits.praw_uinstruction == raw);
}
void edge_step() {
  rising();
  settle();
}

void drive() {}

void observe() {}

void falling_update() {}

void stimulus() {
  reset = 1;
  packets_in = {};
  fetched_in = {UINT64_C(1)};
  clear_in = {};
  {
    for (int repeat_index = 0; repeat_index < (2); ++repeat_index)
      edge_step();
    falling();
    reset = 0;
    offer(UINT64_C(256), UINT64_C(0x100093));
    expect_instruction(UINT64_C(256),
                       UINT64_C(0x100093)); // No capture edge before Decode.
    CHECK(packets_out.pready);
    edge_step();
    offer(UINT64_C(260), UINT64_C(0x850085));
    expect_instruction(UINT64_C(260), UINT64_C(133));
    edge_step();
    falling();
    packets_in.pvalid = 0;
    fetched_in.pready = 0;
    settle();
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
      expect_instruction(UINT64_C(262), UINT64_C(133));
      CHECK(!packets_out.pready);
      edge_step();
    }
    falling();
    fetched_in.pready = 1;
    settle();
    expect_instruction(UINT64_C(262), UINT64_C(133));
    edge_step();

    // A straddling first half enters residual storage even with Decode stalled.
    falling();
    fetched_in.pready = 0;
    offer(UINT64_C(4094), UINT64_C(0x3130001), UINT64_C(2));
    CHECK(!fetched_out.pvalid && packets_out.pready);
    edge_step();
    offer(UINT64_C(4096), UINT64_C(0x850010));
    expect_instruction(UINT64_C(4094), UINT64_C(0x100313));
    CHECK(!packets_out.pready);
    edge_step();
    falling();
    fetched_in.pready = 1;
    settle();
    expect_instruction(UINT64_C(4094), UINT64_C(0x100313));
    edge_step();
    falling();
    packets_in.pvalid = 0;
    settle();
    expect_instruction(UINT64_C(4098), UINT64_C(133));
    edge_step();

    offer(UINT64_C(8190), UINT64_C(0x3130001), UINT64_C(2));
    edge_step();
    offer(UINT64_C(8192), 0);
    packets_in.pbits.ppage_ufault = 1;
    settle();
    CHECK(fetched_out.pvalid && fetched_out.pbits.ppc == UINT64_C(8190) &&
          fetched_out.pbits.pinstruction_upage_ufault &&
          fetched_out.pbits.pinstruction_ufault_uaddress == UINT64_C(8192));
    edge_step();
    offer(UINT64_C(12288), UINT64_C(0x850085));
    edge_step();
    falling();
    clear_in.pvalid = 1;
    settle();
    CHECK(!fetched_out.pvalid && !packets_out.pready);
    edge_step();
    falling();
    clear_in.pvalid = 0;
    packets_in.pvalid = 0;
    settle();
    CHECK(!fetched_out.pvalid);

    throw Finished{};
  }
}

int main() {
  return run_test([] {
    cycle_limit = 202;
    try {
      stimulus();
      for (;;)
        rising();
    } catch (const Finished &) {
    }
  });
}
