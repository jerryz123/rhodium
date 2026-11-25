// Preserves the rv5stage-zawrs cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
// Verifies WB-owned WRS waiting, timeout, interrupt wake, and precise retirement.

bool fault;

bool response_valid = 0;

std::uint32_t response_word;

bool reservation;

int scenario, cycles, wait_cycles, stores;

bool saw_wrs, done;

constexpr int wake_after_entry = 1, timeout_trap = 4;
constexpr int entry_loss = 9, timeout_race = 11, adjacent_waits = 12;
constexpr int timeout_edges = 4096;
int captures, active_edges, completions, signature_fetches;
bool blocked_issue, issued;

extern "C" void observe_decode(std::uint64_t resetting, std::uint64_t valid,
    std::uint64_t ready, std::uint64_t cleared) {
  issued = !resetting && valid && ready && !cleared;
}

extern "C" void observe_wait(std::uint64_t resetting, std::uint64_t capture,
    std::uint64_t active, std::uint64_t reserved, std::uint64_t irq,
    std::uint64_t valid, std::uint64_t illegal, std::uint64_t virt) {
  blocked_issue = !resetting && (capture || active);
  if (resetting) { captures = active_edges = completions = 0; return; }
  if (scenario == wake_after_entry || scenario == timeout_trap) {
    const auto& fetch = instruction_access_out.prequest;
    if (capture) {
      CHECK(!instruction_access_out.pflush && fetch.pvalid && instruction_access_in.prequest.pready);
      CHECK(fetch.pbits.paddress == (scenario == wake_after_entry ? 80 : 76));
    }
    if (active && scenario == timeout_trap && valid) {
      CHECK(illegal && instruction_access_out.pflush && fetch.pvalid && instruction_access_in.prequest.pready);
      CHECK(fetch.pbits.paddress == 256); // trap-vector restart on the completion edge
    } else if (active) {
      CHECK(!fetch.pvalid);
    }
  }
  if (capture) {
    CHECK(!active);
    ++captures;
    active_edges = 0;
    if (scenario == entry_loss || (scenario == adjacent_waits && captures == 1))
      CHECK(!reserved);
  }
  if (!active) { CHECK(!valid); return; }
  if (scenario == timeout_race && active_edges == timeout_edges - 1)
    CHECK(irq && reserved && !valid); // terminal edge, wake beats the NTO trap
  if (scenario == timeout_race && active_edges == timeout_edges)
    CHECK(!irq && valid && !illegal && !virt);
  if ((scenario == entry_loss || (scenario == adjacent_waits && captures == 1)) && !active_edges)
    CHECK(reserved && valid && !illegal && !virt); // loss on capture only
  ++active_edges;
  completions += valid;
}

std::uint32_t instruction_at(std::uint64_t address) {
  switch (address) {
  case 0: {
    return UINT64_C(0x10000093); // mtvec = 0x100
  } break;
  case 4: {
    return UINT64_C(0x30509073);
  } break;
  case 8: {
    return UINT64_C(0x8000093); // locally enable MTIP
  } break;
  case 12: {
    return UINT64_C(0x30409073);
  } break;
  case 16: {
    return UINT64_C(0x700093); // allow U-mode counter reads
  } break;
  case 20: {
    return UINT64_C(0x30609073);
  } break;
  case 24: {
    return UINT64_C(0x10609073);
  } break;
  case 28: {
    return (scenario == 4 || scenario == 5 || scenario == 7 || scenario == timeout_race)
               ? UINT64_C(0x2000b7)
               : UINT64_C(147); // TW
  } break;
  case 32: {
    return scenario == 8 ? UINT64_C(0x800093) : UINT64_C(32915);
  } break;
  case 36: {
    return UINT64_C(0x30009073); // mstatus
  } break;
  case 40: {
    return UINT64_C(0x3c00093); // mepc = 60
  } break;
  case 44: {
    return UINT64_C(0x34109073);
  } break;
  case 48: {
    return ((scenario >= 4 && scenario <= 6) || scenario == timeout_race)
               ? UINT64_C(0x30200073) : UINT64_C(19);
  } break;
  case 52: {
    return UINT64_C(19);
  } break;
  case 56: {
    return (scenario == 10 || scenario == adjacent_waits) ? UINT64_C(0xc02022f3) : UINT64_C(19);
  } break;
  case 60: {
    if (scenario == adjacent_waits) return UINT64_C(0xd00073); // first WRS.NTO
    return scenario == 10
               ? UINT64_C(0x80006f)
               : UINT64_C(0xc02022f3); // skip speculative WRS or read instret
  } break;
  case 64: {
    return (scenario == 3 || scenario == 5) ? UINT64_C(0x1d00073)
                                            : UINT64_C(0xd00073);
  } break;
  case 68: {
    return UINT64_C(0xc0202373); // csrr x6, instret
  } break;
  case 72: {
    return UINT64_C(0x40530333); // sub x6, x6, x5
  } break;
  case 76: {
    return scenario == wake_after_entry ? UINT64_C(19) : UINT64_C(0x603023);
  } break;
  case 80: {
    return scenario == wake_after_entry ? UINT64_C(0x603023) : UINT64_C(111);
  } break;
  case 256: {
    return UINT64_C(0xb0202373); // handler samples minstret first
  } break;
  case 260: {
    return UINT64_C(0x40530333);
  } break;
  case 264: {
    return UINT64_C(0x603423); // retirement delta at 8
  } break;
  case 268: {
    return UINT64_C(0x34202373);
  } break;
  case 272: {
    return UINT64_C(0x603823); // mcause at 16
  } break;
  case 276: {
    return UINT64_C(0x34102373);
  } break;
  case 280: {
    return UINT64_C(0x603c23); // mepc at 24
  } break;
  case 284: {
    return UINT64_C(0x34302373);
  } break;
  case 288: {
    return UINT64_C(0x2603023); // mtval at 32
  } break;
  case 292: {
    return UINT64_C(111);
  } break;
  default: {
    return UINT64_C(19);
  } break;
  }
}

void drive() {
  {
    instruction_access_in = {};
    // Accept the younger signature store on WRS capture even when the
    // preceding word's response occupies this edge. A TW trap must discard it.
    const bool replace_response = scenario == timeout_trap &&
        instruction_access_out.prequest.pbits.paddress == 76 && instruction_access_out.presponse.pready;
    instruction_access_in.prequest.pready =
        instruction_access_out.pflush || !response_valid || replace_response;
    instruction_access_in.presponse.pvalid = response_valid;
    instruction_access_in.presponse.pbits.pword = response_word;
    data_access_in = {};
    data_access_in.prequest.pready = 1;
    data_access_in.pdrained = 1;
    data_access_in.preservation_uvalid = reservation;
  }
}

void observe() {
  {
    if (reset) {
      defer(response_valid, 0);
      defer(cycles, 0);
      defer(wait_cycles, 0);
      defer(saw_wrs, 0);
      defer(stores, 0);
      defer(signature_fetches, 0);
      defer(done, 0);
    } else {
      defer(cycles, cycles + 1);
      if (instruction_access_out.pflush ||
          (response_valid && instruction_access_out.presponse.pready))
        defer(response_valid, 0);
      if (instruction_access_out.prequest.pvalid &&
          instruction_access_in.prequest.pready) {
        defer(response_valid, 1);
        defer(response_word,
              instruction_at(instruction_access_out.prequest.pbits.paddress));
        if (scenario == wake_after_entry && instruction_access_out.prequest.pbits.paddress == 80)
          defer(signature_fetches, signature_fetches + 1);
        if (instruction_access_out.prequest.pbits.paddress == 64)
          defer(saw_wrs, 1);
      }
      if (saw_wrs)
        defer(wait_cycles, wait_cycles + 1);
      if (data_access_out.prequest.pvalid) {
        CHECK(data_access_out.prequest.pbits.paccess == 2);
        defer(stores, stores + 1);
        switch (data_access_out.prequest.pbits.paddress) {
        case 0: {
          {
            CHECK(scenario != 4 && scenario != 8);
            CHECK(data_access_out.prequest.pbits.pdata == (scenario == adjacent_waits ? 3 : 2));
            if (scenario == 3 || scenario == 5)
              CHECK(wait_cycles >= 4096 && wait_cycles < 4200);
            defer(done, 1);
          }
        } break;
        case 8: {
          CHECK(data_access_out.prequest.pbits.pdata ==
                (scenario == 4 ? 1 : 2));
        } break;
        case 16: {
          CHECK(data_access_out.prequest.pbits.pdata ==
                (scenario == 4 ? UINT64_C(2) : UINT64_C(0x8000000000000007)));
        } break;
        case 24: {
          CHECK(data_access_out.prequest.pbits.pdata ==
                (scenario == 4 ? UINT64_C(64) : UINT64_C(68)));
        } break;
        case 32: {
          {
            CHECK(scenario == 4 || scenario == 8);
            CHECK(data_access_out.prequest.pbits.pdata ==
                  (scenario == 4 ? UINT64_C(0xd00073) : 0));
            CHECK(stores == 3);
            defer(done, 1);
          }
        } break;
        default: {
          fail(1, "unexpected store");
        } break;
        }
      }
      CHECK(!fault && cycles < 5000);
    }
  }
}

void falling_update() {
  CHECK(!(blocked_issue && issued));
}

void stimulus() {
  reset = 1;
  hart_id = 0;
  time_counter = 0;
  {
    interrupts = {};
    reservation = 0;
    for (scenario = 0; scenario <= adjacent_waits; scenario++) {
      reset = 1;
      reservation = scenario != 0 && scenario != adjacent_waits;
      interrupts = {};
      if (scenario == 2)
        interrupts.pmachine_utimer = 1;
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      reset = 0;
      if (scenario == adjacent_waits) {
        until([&] { return captures == 1; });
        falling();
        reservation = 1;
        until([&] { return captures == 2 && active_edges == 8; });
        CHECK(completions == 1 && !done);
        falling();
        reservation = 0;
        rising();
        falling();
        reservation = 1;
      }
      until([&] { return saw_wrs || done; });
      falling();
      if (scenario == entry_loss) {
        reservation = 0;
        until([&] { return captures == 1; });
        falling();
        reservation = 1; // restore before the first active edge
      }
      if (scenario == timeout_race) {
        until([&] { return active_edges == timeout_edges - 1; });
        falling();
        interrupts.pmachine_utimer = 1;
        rising();
        falling();
        interrupts.pmachine_utimer = 0;
      }
      if (scenario == 1 || scenario == 6 || scenario == 7 || scenario == 8) {
        for (int repeat_index = 0; repeat_index < (scenario == 7 ? 4300 : 80);
             ++repeat_index)
          falling();
        CHECK(!done && stores == 0);
        if (scenario == 8)
          interrupts.pmachine_utimer = 1;
        else
          reservation = 0;
      }
      until([&] { return done; });
      if (scenario == wake_after_entry || scenario == timeout_trap)
        CHECK(captures == 1 && completions == 1);
      if (scenario == wake_after_entry) CHECK(signature_fetches == 1 && stores == 1);
      if (scenario == timeout_race) CHECK(completions == 1 && active_edges == timeout_edges + 1);
      if (scenario == adjacent_waits) CHECK(captures == 2 && completions == 2);
      falling();
    }
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
