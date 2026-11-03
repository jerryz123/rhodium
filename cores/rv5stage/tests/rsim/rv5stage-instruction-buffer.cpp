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

enum ParcelOwner : unsigned { None, Lower, Upper, Previous };

void predict(ParcelOwner owner, std::uint64_t target, unsigned action) {
  packets_in.pbits.pprediction = {static_cast<std::uint8_t>(owner), target,
                                  static_cast<std::uint8_t>(action)};
  settle();
}

void expect_prediction(std::uint64_t target, unsigned action) {
  CHECK(fetched_out.pvalid && fetched_out.pbits.ppredicted_unext_upc == target &&
        fetched_out.pbits.ppredicted_uras_uaction == action);
}

void check_parcel_ownership() {
  // A prediction for the upper compressed instruction survives the lower
  // instruction's transfer, Decode backpressure, and a different live prediction.
  offer(64, 0x80820085); // C.ADDI then C.JR x1.
  predict(Upper, 0x180, 2);
  expect_prediction(66, 0);
  edge_step();
  offer(0x180, 0x00859082); // C.JALR x1 then C.ADDI.
  predict(Lower, 0x240, 1);
  fetched_in.pready = 0;
  settle();
  for (int i = 0; i < 3; ++i) {
    expect_instruction(66, 0x8082);
    expect_prediction(0x180, 2);
    CHECK(!packets_out.pready);
    edge_step();
  }
  falling();
  fetched_in.pready = 1;
  settle();
  edge_step();
  expect_prediction(0x240, 1);
  edge_step();
  falling();
  packets_in.pvalid = 0;
  settle();
  expect_prediction(0x184, 0); // Lower prediction cannot apply to retained upper.
  edge_step();

  // The next fetch can revisit the very same PC. A new hint at that address
  // belongs to the new occurrence, not to the older buffered instruction.
  offer(64, 0xa0010085);
  predict(Upper, 66, 0);
  edge_step();
  offer(66, 0xa0010085, 2);
  predict(Upper, 0x1c0, 0);
  expect_prediction(66, 0);
  CHECK(!packets_out.pready);
  edge_step();
  expect_prediction(0x1c0, 0);
  CHECK(packets_out.pready);
  edge_step();

  // A page-crossing branch can first be predicted on its continuation.
  // A faulting continuation must still report the second address and ignore the hint.
  for (bool fault : {false, true}) {
    offer(4094, 0x00ef0001, 2); // JAL x1 starts at the page's last halfword.
    predict(None, 0, 0);
    edge_step();
    offer(4096, 0, 1);
    predict(Previous, 0x380, 1);
    packets_in.pbits.paccess_ufault = fault;
    fetched_in.pready = 0;
    settle();
    expect_instruction(4094, 0xef);
    expect_prediction(fault ? 4098 : 0x380, fault ? 0 : 1);
    CHECK(fetched_out.pbits.pinstruction_uaccess_ufault == fault);
    if (fault) CHECK(fetched_out.pbits.pinstruction_ufault_uaddress == 4096);
    edge_step();
    falling();
    fetched_in.pready = 1;
    settle();
    edge_step();
  }
}

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
    if (!with_c) {
      // The same first parcel would be C.ADDI with C enabled. Here it is one
      // unexpanded word: normal instruction decode, not expansion, decides legality.
      offer(252, 0x850085);
      expect_instruction(252, 0x850085);
      CHECK(fetched_out.pbits.pinstruction == 0x850085 &&
            fetched_out.pbits.psequential_upc == 256 && !fetched_out.pbits.pcompressed_uillegal);
      edge_step();
      falling();
      packets_in.pvalid = 0;
      settle();
      CHECK(!fetched_out.pvalid);
      throw Finished{};
    }

    offer(248, 0, 1); // Reserved compressed encoding.
    expect_instruction(248, 0);
    CHECK(fetched_out.pbits.pcompressed_uillegal && fetched_out.pbits.psequential_upc == 250);
    packets_in.pbits.ppage_ufault = 1;
    settle();
    CHECK(fetched_out.pbits.pinstruction_upage_ufault && !fetched_out.pbits.pcompressed_uillegal &&
          fetched_out.pbits.psequential_upc == 252 && fetched_out.pbits.pinstruction == 0);
    edge_step();
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

    check_parcel_ownership();
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
