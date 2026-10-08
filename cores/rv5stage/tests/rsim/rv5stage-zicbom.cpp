// Preserves the rv5stage-zicbom cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
// Verifies WB-only CMO dispatch, precise completion, arithmetic resumption, privilege policy, and squash.

bool instruction_response_valid;

std::uint32_t instruction_response_bits;

int scenario, attempts, accepted, pending_cycles, stores;

bool done;

std::uint32_t cbo_instruction() {
  if (scenario == 0)
    return UINT64_C(0x10a00f); // clean
  if (scenario == 2)
    return UINT64_C(0x20a00f); // flush
  return UINT64_C(40975);      // invalidate
}
std::uint32_t instruction_at(std::uint64_t address) {
  if (scenario == 11) {
    switch (address) {
    case UINT64_C(256): {
      return UINT64_C(0x3f00093); // x1 = 63
    } break;
    case UINT64_C(260): {
      return UINT64_C(0xff900293); // x5 = -7
    } break;
    case UINT64_C(264): {
      return UINT64_C(0x900313); // x6 = 9
    } break;
    case UINT64_C(268): {
      return UINT64_C(40975); // cbo.inval (x1)
    } break;
    case UINT64_C(272): {
      return UINT64_C(0x26283b3); // mul x7, x5, x6
    } break;
    case UINT64_C(276): {
      return UINT64_C(0x263c433); // div x8, x7, x6
    } break;
    case UINT64_C(280): {
      return UINT64_C(0x2703023); // sd x7, 32(x0)
    } break;
    case UINT64_C(284): {
      return UINT64_C(0x2803423); // sd x8, 40(x0)
    } break;
    default: {
      return UINT64_C(111);
    } break;
    }
  }
  switch (address) {
  case UINT64_C(0): {
    return UINT64_C(0x342021f3); // csrr x3, mcause
  } break;
  case UINT64_C(4): {
    return UINT64_C(0x303423); // sd x3, 8(x0)
  } break;
  case UINT64_C(8): {
    return UINT64_C(0x34302273); // csrr x4, mtval
  } break;
  case UINT64_C(12): {
    return UINT64_C(0x403823); // sd x4, 16(x0)
  } break;
  case UINT64_C(16): {
    return UINT64_C(111);
  } break;
  case UINT64_C(256): {
    return UINT64_C(0x3f00093); // x1 = unaligned address 63
  } break;
  case UINT64_C(260): {
    return scenario == 10 ? UINT64_C(0x340006f) : UINT64_C(19);
  } break;
  case UINT64_C(264): {
    return scenario == 7
               ? UINT64_C(0x1000113)
               : (scenario >= 8 ? UINT64_C(0x3000113) : UINT64_C(275));
  } break;
  case UINT64_C(268): {
    return UINT64_C(0x30a11073); // csrw menvcfg, x2
  } break;
  case UINT64_C(272): {
    return scenario >= 6  ? UINT64_C(0x80006f)
           : scenario < 3 ? UINT64_C(0x100006f)
                          : UINT64_C(0x200006f);
  } break;
  case UINT64_C(280): {
    return UINT64_C(0x13000113); // x2 = lower-privilege entry
  } break;
  case UINT64_C(284): {
    return UINT64_C(0x34111073); // csrw mepc, x2
  } break;
  case UINT64_C(288): {
    return scenario < 3    ? UINT64_C(0xb02022f3)
           : scenario == 9 ? UINT64_C(275)
                           : UINT64_C(4407);
  } break;
  case UINT64_C(292): {
    return scenario < 3 ? UINT64_C(0xc0006f)
                        : UINT64_C(0x115113); // skip to CMO, or MPP = S/U
  } break;
  case UINT64_C(296): {
    return UINT64_C(0x30011073); // csrw mstatus, x2
  } break;
  case UINT64_C(300): {
    return UINT64_C(0x30200073);
  } break;
  case UINT64_C(304): {
    return cbo_instruction();
  } break;
  case UINT64_C(308): {
    return scenario < 3 ? UINT64_C(0xb0202373)
                        : UINT64_C(19); // read retired count again
  } break;
  case UINT64_C(312): {
    return scenario < 3 ? UINT64_C(0x40530333) : UINT64_C(0x2003023);
  } break;
  case UINT64_C(316): {
    return UINT64_C(0x2603023); // observable counter delta, no fence
  } break;
  default: {
    return UINT64_C(111);
  } break;
  }
}

void drive() {
  {
    instruction_access_in = {};
    instruction_access_in.prequest.pready =
        instruction_access_out.pflush || !instruction_response_valid;
    instruction_access_in.presponse.pvalid = instruction_response_valid;
    instruction_access_in.presponse.pbits.pword = instruction_response_bits;
  }
  data_access_in.prequest.pready =
      data_access_out.prequest.pbits.paccess < 7 || attempts >= 3;
  data_access_in.prequest_ufault =
      data_access_out.prequest.pvalid &&
      data_access_out.prequest.pbits.paccess >= 7 && scenario == 5;
  data_access_in.prequest_uaccess_ufault =
      data_access_out.prequest.pvalid &&
      data_access_out.prequest.pbits.paccess >= 7 && scenario == 4;
  data_access_in.presponse.pvalid = pending_cycles == 1;
  data_access_in.presponse.pbits = {};
  data_access_in.presponse.pbits.paccess_ufault = scenario == 3;
  data_access_in.pdrained = pending_cycles == 0;
  data_access_in.preservation_uvalid = UINT64_C(0);
}

void observe() {
  {
    if (reset) {
      defer(instruction_response_valid, 0);
      defer(instruction_response_bits, 0);
      defer(attempts, 0);
      defer(accepted, 0);
      defer(pending_cycles, 0);
      defer(stores, 0);
      defer(done, 0);
    } else {
      if (instruction_access_out.pflush ||
          (instruction_response_valid &&
           instruction_access_out.presponse.pready))
        defer(instruction_response_valid, 0);
      if (instruction_access_out.prequest.pvalid &&
          instruction_access_in.prequest.pready) {
        defer(instruction_response_valid, 1);
        defer(instruction_response_bits,
              instruction_at(instruction_access_out.prequest.pbits.paddress));
      }
      if (pending_cycles != 0)
        defer(pending_cycles, pending_cycles - 1);
      if (data_access_out.prequest.pvalid &&
          data_access_out.prequest.pbits.paccess >= 7) {
        CHECK(scenario != 6 && scenario != 9 && scenario != 10 &&
              data_access_out.prequest.pbits.paddress == 63 &&
              data_access_out.prequest.pbits.pcontext.pwriteback == 0);
        CHECK(data_access_out.prequest.pbits.paccess ==
              (scenario == 0                    ? 8
               : scenario == 2 || scenario == 7 ? 9
                                                : 7));
        defer(attempts, attempts + 1);
        if (data_access_in.prequest.pready && !data_access_in.prequest_ufault &&
            !data_access_in.prequest_uaccess_ufault) {
          CHECK(accepted == 0);
          defer(accepted, accepted + 1);
          defer(pending_cycles, 24);
        }
      }
      if (data_access_out.prequest.pvalid &&
          data_access_out.prequest.pbits.paccess == 2) {
        CHECK(pending_cycles == 0);
        if (scenario == 11) {
          CHECK(accepted == 1 && attempts == 4);
          CHECK(data_access_out.prequest.pbits.paddress ==
                    (stores == 0 ? 32 : 40) &&
                data_access_out.prequest.pbits.pdata ==
                    (stores == 0 ? UINT64_C(0xffffffffffffffc1)
                                 : UINT64_C(0xfffffffffffffff9)));
          if (stores == 1)
            defer(done, 1);
          defer(stores, stores + 1);
        } else if (scenario < 3 || scenario == 7 || scenario == 8 ||
                   scenario == 10) {
          CHECK(data_access_out.prequest.pbits.paddress == 32 &&
                accepted == (scenario == 10 ? 0 : 1) &&
                attempts == (scenario == 10 ? 0 : 4));
          if (scenario < 3)
            CHECK(data_access_out.prequest.pbits.pdata == 3);
          defer(done, 1);
        } else if (stores == 0) {
          CHECK(data_access_out.prequest.pbits.paddress == 8 &&
                data_access_out.prequest.pbits.pdata ==
                    (scenario == 3 || scenario == 4 ? 7
                     : scenario == 5                ? 15
                                                    : 2) &&
                accepted == (scenario == 3 ? 1 : 0));
          defer(stores, 1);
        } else {
          CHECK(data_access_out.prequest.pbits.paddress == 16 &&
                data_access_out.prequest.pbits.pdata ==
                    (scenario == 6 || scenario == 9
                         ? (field(UINT64_C(0), 32, 32) |
                            field(cbo_instruction(), 32, 0))
                         : UINT64_C(63)));
          defer(done, 1);
        }
      }
    }
  }
}

void falling_update() {}

void stimulus() {
  reset = 1;
  time_counter = 0;
  hart_id = 0;
  {
    interrupts = {};
    for (scenario = 0; scenario <= 11; scenario++) {
      reset = 1;
      for (int repeat_index = 0; repeat_index < (2); ++repeat_index)
        rising();
      falling();
      reset = 0;
      rising();
      falling();
      for (int cycles = 0; cycles < 1500 && !done; cycles++) {
        rising();
        settle();
      }
      CHECK(done);
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
