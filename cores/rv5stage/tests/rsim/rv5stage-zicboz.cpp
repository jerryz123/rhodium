// Preserves the rv5stage-zicboz cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
// Verifies CBO.ZERO replay, single retirement, fence ordering, and precise faults.

constexpr std::uint8_t MEMORY_LOAD = UINT64_C(1);
constexpr std::uint8_t MEMORY_STORE = UINT64_C(2);

bool instruction_response_valid;

std::uint32_t instruction_response_bits;

int scenario = 0;

int attempts, accepted, pending_cycles, stores;

bool done;

std::uint32_t instruction_at(std::uint64_t address) {
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
    return UINT64_C(0x3f00093); // addi x1, x0, 63
  } break;
  case UINT64_C(260): {
    return scenario == 3 ? UINT64_C(0x12000113) : UINT64_C(0x1c0006f);
  } break;
  case UINT64_C(264): {
    return UINT64_C(0x34111073); // csrw mepc, x2
  } break;
  case UINT64_C(268): {
    return UINT64_C(4407); // lui x2, 1
  } break;
  case UINT64_C(272): {
    return UINT64_C(0x115113); // srli x2, x2, 1 (MPP=S)
  } break;
  case UINT64_C(276): {
    return UINT64_C(0x30011073); // csrw mstatus, x2
  } break;
  case UINT64_C(280): {
    return UINT64_C(0x30200073); // mret
  } break;
  case UINT64_C(288): {
    return UINT64_C(0x40a00f); // cbo.zero (x1)
  } break;
  case UINT64_C(292): {
    return UINT64_C(0xff0000f); // fence iorw, iorw
  } break;
  case UINT64_C(296): {
    return UINT64_C(0x2003023); // sd x0, 32(x0)
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
    data_access_in = {};
    data_access_in.prequest.pready =
        data_access_out.prequest.pbits.paccess != UINT64_C(6) ||
        (scenario == 0 && attempts >= 3);
    data_access_in.prequest_ufault =
        data_access_out.prequest.pvalid &&
        data_access_out.prequest.pbits.paccess == UINT64_C(6) && scenario == 2;
    data_access_in.prequest_uaccess_ufault =
        data_access_out.prequest.pvalid &&
        data_access_out.prequest.pbits.paccess == UINT64_C(6) && scenario == 1;
    data_access_in.presponse.pvalid = pending_cycles == 1;
    data_access_in.pdrained = pending_cycles == 0;
    data_access_in.preservation_uvalid = UINT64_C(0);
  }
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
          data_access_out.prequest.pbits.paccess == UINT64_C(6)) {
        CHECK(scenario != 3 && data_access_out.prequest.pbits.paddress == 63 &&
              bit_slice(data_access_out.prequest.pbits.pcontext.pwriteback, 7,
                        2) == 0);
        defer(attempts, attempts + 1);
        if (data_access_in.prequest.pready) {
          CHECK(accepted == 0);
          defer(accepted, accepted + 1);
          defer(pending_cycles, 16);
        }
      }
      if (data_access_out.prequest.pvalid &&
          data_access_out.prequest.pbits.paccess == MEMORY_STORE) {
        if (scenario == 0) {
          CHECK(data_access_out.prequest.pbits.paddress == 32 &&
                accepted == 1 && attempts == 4 && pending_cycles == 0);
          defer(done, 1);
        } else if (stores == 0) {
          CHECK(data_access_out.prequest.pbits.paddress == 8 &&
                data_access_out.prequest.pbits.pdata ==
                    (scenario == 1   ? UINT64_C(7)
                     : scenario == 2 ? UINT64_C(15)
                                     : UINT64_C(2)) &&
                accepted == 0);
          defer(stores, 1);
        } else {
          CHECK(data_access_out.prequest.pbits.paddress == 16 &&
                data_access_out.prequest.pbits.pdata ==
                    (scenario == 3 ? UINT64_C(0x40a00f) : UINT64_C(63)));
          defer(done, 1);
        }
      }
    }
  }
}

void falling_update() {}

void stimulus() {
  reset = UINT64_C(1);
  time_counter = {};
  hart_id = {};
  {
    interrupts = {};
    for (scenario = 0; scenario < 4; scenario++) {
      reset = 1;
      for (int repeat_index = 0; repeat_index < (2); ++repeat_index)
        rising();
      falling();
      reset = 0;
      rising();
      falling();
      for (int cycles = 0; cycles < 600 && !done; cycles++) {
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
