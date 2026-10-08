// Preserves the rv5stage-pause cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
// Checks bounded PAUSE throttling, retirement, squash, interrupt exit, and older-load independence.

bool fault;

bool response_valid = 0;

std::uint32_t response_word;

int scenario, cycles, stores, baseline_cycles, single_cycles, load_age;

bool done, load_pending;

std::uint32_t instruction_at(std::uint64_t address) {
  if (scenario == 7) {
    switch (address) {
    case 0: {
      return UINT64_C(0x2000b7); // mstatus.TW must not restrict PAUSE
    } break;
    case 4: {
      return UINT64_C(0x30009073);
    } break;
    case 8: {
      return UINT64_C(0x2000093);
    } break;
    case 12: {
      return UINT64_C(0x34109073); // mepc = 32, MPP = U
    } break;
    case 16: {
      return UINT64_C(0x30200073);
    } break;
    case 32: {
      return UINT64_C(0x100000f);
    } break;
    case 36: {
      return UINT64_C(19);
    } break;
    case 40: {
      return UINT64_C(0x300313);
    } break;
    case 44: {
      return UINT64_C(19);
    } break;
    case 48: {
      return UINT64_C(0x603023);
    } break;
    default: {
      return UINT64_C(111);
    } break;
    }
  }
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
    return scenario == 3 ? UINT64_C(0x800093) : UINT64_C(147);
  } break;
  case 20: {
    return UINT64_C(0x30009073);
  } break;
  case 24: {
    return (scenario == 6 || scenario == 8)
               ? UINT64_C(0x20003a03)
               : UINT64_C(19); // delayed ld x20, 512(x0)
  } break;
  case 28: {
    return (scenario == 6 || scenario == 8)
               ? UINT64_C(19)
               : UINT64_C(
                     3223331571); // sample instret without draining the delayed load
  } break;
  case 32: {
    return scenario == 0   ? UINT64_C(19)
           : scenario == 4 ? UINT64_C(0x80006f)
           : scenario == 5 ? UINT64_C(0xffffffff)
                           : UINT64_C(0x100000f);
  } break;
  case 36: {
    return (scenario == 2 || scenario == 4 || scenario == 5)
               ? UINT64_C(0x100000f)
               : UINT64_C(19);
  } break;
  case 40: {
    return scenario == 8   ? UINT64_C(0xa0313)
           : scenario == 6 ? UINT64_C(0x300313)
                           : UINT64_C(0xc0202373);
  } break;
  case 44: {
    return (scenario == 6 || scenario == 8)
               ? UINT64_C(19)
               : UINT64_C(0x40530333); // retirement delta
  } break;
  case 48: {
    return UINT64_C(0x603023);
  } break;
  case 52: {
    return UINT64_C(111);
  } break;
  case 256: {
    return UINT64_C(0xb0202373); // handler retirement delta
  } break;
  case 260: {
    return UINT64_C(0x40530333);
  } break;
  case 264: {
    return UINT64_C(0x603423);
  } break;
  case 268: {
    return UINT64_C(0x34202373);
  } break;
  case 272: {
    return UINT64_C(0x603823);
  } break;
  case 276: {
    return UINT64_C(0x34102373);
  } break;
  case 280: {
    return UINT64_C(0x603c23);
  } break;
  case 284: {
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
    data_access_in.pdrained = !load_pending;
    data_access_in.presponse.pvalid =
        load_pending && load_age >= (scenario == 8 ? 12 : 120);
    data_access_in.presponse.pbits.pcontext.pwriteback =
        memory_integer(UINT64_C(20));
    data_access_in.presponse.pbits.pdata = UINT64_C(4660);
  }
}

void observe() {
  {
    if (reset) {
      defer(response_valid, 0);
      defer(cycles, 0);
      defer(stores, 0);
      defer(done, 0);
      defer(load_pending, 0);
      defer(load_age, 0);
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
      }
      if (load_pending) {
        defer(load_age, load_age + 1);
        if (data_access_in.presponse.pvalid && data_access_out.presponse.pready)
          defer(load_pending, 0);
      }
      if (data_access_out.prequest.pvalid) {
        if (data_access_out.prequest.pbits.paccess == 1) {
          CHECK((scenario == 6 || scenario == 8) &&
                data_access_out.prequest.pbits.paddress == 512 &&
                !load_pending);
          defer(load_pending, 1);
          defer(load_age, 0);
        } else {
          CHECK(data_access_out.prequest.pbits.paccess == 2);
          defer(stores, stores + 1);
          switch (data_access_out.prequest.pbits.paddress) {
          case 0: {
            {
              CHECK(scenario != 3 && scenario != 5);
              CHECK(data_access_out.prequest.pbits.pdata ==
                    (scenario == 8   ? UINT64_C(4660)
                     : scenario == 4 ? 2
                                     : 3));
              if (scenario == 0)
                baseline_cycles = cycles;
              if (scenario == 1) {
                single_cycles = cycles;
                CHECK(cycles >= baseline_cycles + 16 &&
                      cycles <= baseline_cycles + 28);
              }
              if (scenario == 2)
                CHECK(cycles >= single_cycles + 16 &&
                      cycles <= single_cycles + 28);
              if (scenario == 4)
                CHECK(cycles < baseline_cycles + 16);
              if (scenario == 6)
                CHECK(load_pending && load_age < 120);
              if (scenario == 7)
                CHECK(privilege == 0 && bit_slice(mstatus, 21, 1));
              defer(done, 1);
            }
          } break;
          case 8: {
            CHECK(data_access_out.prequest.pbits.pdata ==
                  (scenario == 5 ? 1 : 2));
          } break;
          case 16: {
            CHECK(data_access_out.prequest.pbits.pdata ==
                  (scenario == 5 ? UINT64_C(2) : UINT64_C(0x8000000000000007)));
          } break;
          case 24: {
            {
              CHECK(data_access_out.prequest.pbits.pdata ==
                    (scenario == 5 ? 32 : 36));
              CHECK(stores == 2);
              defer(done, 1);
            }
          } break;
          default: {
            fail(1, "unexpected signature address");
          } break;
          }
        }
      }
      CHECK(!fault && cycles < 500);
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
    for (scenario = 0; scenario < 9; scenario++) {
      reset = 1;
      interrupts = {};
      for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
        falling();
      reset = 0;
      if (scenario == 3) {
        until([&] { return cycles == baseline_cycles + 1; });
        falling();
        interrupts.pmachine_utimer = 1;
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
