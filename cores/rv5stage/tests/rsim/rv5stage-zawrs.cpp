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
    return (scenario == 4 || scenario == 5 || scenario == 7)
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
    return (scenario >= 4 && scenario <= 6) ? UINT64_C(0x30200073)
                                            : UINT64_C(19);
  } break;
  case 52: {
    return UINT64_C(19);
  } break;
  case 56: {
    return scenario == 10 ? UINT64_C(0xc02022f3) : UINT64_C(19);
  } break;
  case 60: {
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
    return UINT64_C(0x603023); // sd x6, 0(x0)
  } break;
  case 80: {
    return UINT64_C(111);
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
    instruction_access_in.prequest.pready =
        instruction_access_out.pflush || !response_valid;
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
            CHECK(data_access_out.prequest.pbits.pdata == 2);
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

void falling_update() {}

void stimulus() {
  reset = 1;
  hart_id = 0;
  time_counter = 0;
  {
    interrupts = {};
    reservation = 0;
    for (scenario = 0; scenario < 11; scenario++) {
      reset = 1;
      reservation = scenario != 0;
      interrupts = {};
      if (scenario == 2)
        interrupts.pmachine_utimer = 1;
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      reset = 0;
      until([&] { return saw_wrs || done; });
      falling();
      if (scenario == 9)
        reservation = 0; // invalidate before WB entry
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
