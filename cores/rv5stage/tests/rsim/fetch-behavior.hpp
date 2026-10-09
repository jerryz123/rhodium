// Preserves the rv5stage-fetch cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "driver.hpp"
using control_t = std::remove_cvref_t<decltype(control_in)>;
using pulse_t = std::remove_cvref_t<decltype(control_in.pflush)>;
using valid_bits64_t = std::remove_cvref_t<decltype(control_in.prestart)>;
using branch_update_t =
    std::remove_cvref_t<decltype(control_in.pbranch_uupdate)>;
using memory_in_t = std::remove_cvref_t<decltype(memory_in)>;
using ready_t = std::remove_cvref_t<decltype(memory_in.prequest)>;
using result_t = std::remove_cvref_t<decltype(memory_in.presponse)>;
using result_bits_t = std::remove_cvref_t<decltype(memory_in.presponse.pbits)>;
using response_bits_t =
    std::remove_cvref_t<decltype(memory_in.presponse.pbits.presponse)>;
using memory_out_t = std::remove_cvref_t<decltype(memory_out)>;
using request_t = std::remove_cvref_t<decltype(memory_out.prequest)>;
using request_bits_t = std::remove_cvref_t<decltype(memory_out.prequest.pbits)>;
using fetched_out_t = std::remove_cvref_t<decltype(fetched_out)>;
using fetched_bits_t = std::remove_cvref_t<decltype(fetched_out.pbits)>;
// Checks fixed-latency fetch assembly, completed-word capacity, restart, and faults.
// SPDX-License-Identifier: Apache-2.0

bool active = UINT64_C(1);

bool flush = UINT64_C(0);

bool restart_valid = UINT64_C(0);

std::uint64_t restart_pc = {};

bool invalidate_all = UINT64_C(0);

bool predictor_flush = UINT64_C(0);

bool response_valid;

struct response_t {
  bool pvalid{};
  response_bits_t pbits{};
};
response_t s2_response{};

response_bits_t response_bits;

bool fetched_ready = UINT64_C(1);

int stalled_requests;

std::uint64_t held_request_address;

std::uint32_t word_at(std::uint64_t address) {
  switch (address) {
  case UINT64_C(0): {
    return UINT64_C(8716421);
  } break;
  case UINT64_C(4): {
    return UINT64_C(51576833);
  } break;
  case UINT64_C(8): {
    return UINT64_C(2416050192);
  } break;
  case UINT64_C(256): {
    return UINT64_C(19);
  } break;
  case UINT64_C(260): {
    return UINT64_C(1048723);
  } break;
  case UINT64_C(264): {
    return UINT64_C(2097427);
  } break;
  case UINT64_C(4092): {
    return UINT64_C(51576833);
  } break;
  default: {
    return UINT64_C(19);
  } break;
  }
}

void expect_instruction(std::uint64_t pc, std::uint64_t sequential_pc,
                        std::uint32_t raw_instruction,
                        std::uint32_t instruction) {
  until([&] { return fetched_out.pvalid; });
  settle();
  CHECK(fetched_out.pbits.ppc == pc &&
        fetched_out.pbits.psequential_upc == sequential_pc &&
        fetched_out.pbits.praw_uinstruction == raw_instruction &&
        fetched_out.pbits.pinstruction == instruction &&
        !fetched_out.pbits.pcompressed_uillegal &&
        !fetched_out.pbits.pinstruction_upage_ufault &&
        !fetched_out.pbits.pinstruction_uaccess_ufault);
  rising();
  settle();
}

void drive() {
  {
    control_in.pactive = active;
    control_in.pflush.pvalid = flush;
    control_in.prestart.pvalid = restart_valid;
    control_in.prestart.pbits = restart_pc;
    control_in.pinvalidate_uall.pvalid = invalidate_all;
    control_in.ppredictor_uflush.pvalid = predictor_flush;
    control_in.pbranch_uupdate = {};
    memory_in.prequest.pready = UINT64_C(1);
    memory_in.presponse.pvalid = s2_response.pvalid;
    memory_in.presponse.pbits.presponse = s2_response.pbits;
    memory_in.presponse.pbits.preplay = false;
    fetched_in.pready = fetched_ready;
  }
}

inline void (*fetch_before_tick)() = [] {};
void observe() {
  fetch_before_tick();
  {
    if (reset) {
      defer(response_valid, UINT64_C(0));
      defer(s2_response, std::remove_cvref_t<decltype(s2_response)>{});
      defer(response_bits, std::remove_cvref_t<decltype(response_bits)>{});
    } else {
      defer(s2_response, memory_out.pflush || memory_out.ps1_ukill
                             ? response_t{}
                             : response_t{response_valid, response_bits});
      defer(response_valid, UINT64_C(0));
      if (memory_out.prequest.pvalid && memory_in.prequest.pready) {
        defer(response_valid, UINT64_C(1));
        defer(response_bits.pword, word_at(memory_out.prequest.pbits.paddress));
        defer(response_bits.ppage_ufault,
              memory_out.prequest.pbits.paddress == UINT64_C(4096));
        defer(response_bits.paccess_ufault, UINT64_C(0));
        CHECK(bit_slice(memory_out.prequest.pbits.paddress, 0, (1) - (0) + 1) ==
              UINT64_C(0));
      }
    }
  }
}

void falling_update() {}

void stimulus() {
  reset = UINT64_C(1);
  {
    for (int repeat_index = 0; repeat_index < (2); ++repeat_index)
      rising();
    settle();
    reset = UINT64_C(0);
    restart_pc = UINT64_C(0);
    restart_valid = UINT64_C(1);
    rising();
    settle();
    restart_valid = UINT64_C(0);

    expect_instruction(UINT64_C(0), UINT64_C(2), UINT64_C(133),
                       UINT64_C(1081491));
    expect_instruction(UINT64_C(2), UINT64_C(4), UINT64_C(133),
                       UINT64_C(1081491));
    expect_instruction(UINT64_C(4), UINT64_C(6), UINT64_C(1), UINT64_C(19));
    expect_instruction(UINT64_C(6), UINT64_C(10), UINT64_C(1049363),
                       UINT64_C(1049363));
    expect_instruction(UINT64_C(10), UINT64_C(12), UINT64_C(36866),
                       UINT64_C(1048691));

    restart_pc = UINT64_C(256);
    restart_valid = UINT64_C(1);
    rising();
    settle();
    restart_valid = UINT64_C(0);
    // Empty FQ and IBuf must expose this complete S2 result to Decode in
    // the same cycle, before any further storage edge.
    until([&] { return s2_response.pvalid; });
    settle();
    CHECK(fetched_out.pvalid && fetched_out.pbits.ppc == UINT64_C(256) &&
          fetched_out.pbits.pinstruction == UINT64_C(19));
    held_request_address = memory_out.prequest.pbits.paddress;
    active = UINT64_C(0);
    settle();
    CHECK(memory_out.prequest.pbits.paddress == held_request_address);
    active = UINT64_C(1);
    rising();
    settle();
    CHECK(fetched_out.pvalid && fetched_out.pbits.ppc == UINT64_C(260) &&
          fetched_out.pbits.pinstruction == UINT64_C(1048723));
    rising();
    settle();
    CHECK(fetched_out.pvalid && fetched_out.pbits.ppc == UINT64_C(264) &&
          fetched_out.pbits.pinstruction == UINT64_C(2097427));

    fetched_ready = UINT64_C(0);
    stalled_requests = 0;
    restart_pc = UINT64_C(512);
    restart_valid = UINT64_C(1);
    settle();
    CHECK(memory_out.pflush && memory_out.prequest.pvalid &&
          memory_in.prequest.pready &&
          memory_out.prequest.pbits.paddress == UINT64_C(512));
    stalled_requests = 1;
    rising();
    settle();
    restart_valid = UINT64_C(0);
    for (int repeat_index = 0; repeat_index < (24); ++repeat_index) {
      falling();
      if (memory_out.prequest.pvalid && memory_in.prequest.pready)
        stalled_requests = stalled_requests + 1;
      rising();
      settle();
      if (fetched_out.pvalid)
        CHECK(fetched_out.pbits.ppc == UINT64_C(512) &&
              fetched_out.pbits.pinstruction == UINT64_C(19));
    }
    CHECK(stalled_requests == 5);
    CHECK(fetched_out.pvalid && fetched_out.pbits.ppc == UINT64_C(512));
    CHECK(!memory_out.prequest.pvalid);

    fetched_ready = UINT64_C(1);
    expect_instruction(UINT64_C(512), UINT64_C(516), UINT64_C(19),
                       UINT64_C(19));
    expect_instruction(UINT64_C(516), UINT64_C(520), UINT64_C(19),
                       UINT64_C(19));
    expect_instruction(UINT64_C(520), UINT64_C(524), UINT64_C(19),
                       UINT64_C(19));
    expect_instruction(UINT64_C(524), UINT64_C(528), UINT64_C(19),
                       UINT64_C(19));
    expect_instruction(UINT64_C(528), UINT64_C(532), UINT64_C(19),
                       UINT64_C(19));

    restart_pc = UINT64_C(4094);
    restart_valid = UINT64_C(1);
    rising();
    settle();
    restart_valid = UINT64_C(0);
    until([&] { return fetched_out.pvalid; });
    settle();
    CHECK(fetched_out.pbits.ppc == UINT64_C(4094) &&
          fetched_out.pbits.pinstruction_upage_ufault &&
          fetched_out.pbits.pinstruction_ufault_uaddress == UINT64_C(4096));
    // Accept the checked faulting instruction before the next recovery test.
    rising();
    settle();
    // A clear without a PC cannot resume from speculative fetch-ahead state.
    falling();
    flush = UINT64_C(1);
    rising();
    settle();
    flush = UINT64_C(0);
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index) {
      falling();
      CHECK(!memory_out.prequest.pvalid && !fetched_out.pvalid);
    }
    restart_valid = UINT64_C(1);
    restart_pc = UINT64_C(256);
    rising();
    settle();
    restart_valid = UINT64_C(0);
    expect_instruction(UINT64_C(256), UINT64_C(260), UINT64_C(19),
                       UINT64_C(19));
    throw Finished{};
  }
}

void run_fetch_test() {
  cycle_limit = 1000;
  try {
    stimulus();
  } catch (const Finished &) {
  }
}
