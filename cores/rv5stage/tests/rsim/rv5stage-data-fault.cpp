// Preserves the rv5stage-data-fault cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
// Verifies a WB-stage data access fault traps without replaying or reserving its destination.

constexpr std::uint8_t MEMORY_LOAD = UINT64_C(1);
constexpr std::uint8_t MEMORY_STORE = UINT64_C(2);

bool instruction_response_valid;

std::uint32_t instruction_response_bits;

bool faulting_load_seen;

std::uint8_t stores_seen;

std::uint32_t instruction_at(std::uint64_t address) {
  switch (address) {
  case UINT64_C(0): {
    return UINT64_C(0x900293); // addi x5, x0, 9
  } break;
  case UINT64_C(4): {
    return UINT64_C(0x503c23); // sd x5, 24(x0)
  } break;
  case UINT64_C(8): {
    return UINT64_C(0x342020f3); // csrr x1, mcause
  } break;
  case UINT64_C(12): {
    return UINT64_C(0x103423); // sd x1, 8(x0)
  } break;
  case UINT64_C(16): {
    return UINT64_C(0x34302173); // csrr x2, mtval
  } break;
  case UINT64_C(20): {
    return UINT64_C(0x203823); // sd x2, 16(x0)
  } break;
  case UINT64_C(256): {
    return UINT64_C(12931); // ld x5, 0(x0), access fault
  } break;
  case UINT64_C(260): {
    return UINT64_C(0x2003023); // sd x0, 32(x0), must be squashed
  } break;
  default: {
    return UINT64_C(19);
  } break;
  }
}

void drive() {
  {
    instruction_access_in.prequest.pready =
        instruction_access_out.pflush || !instruction_response_valid;
    instruction_access_in.presponse.pvalid = instruction_response_valid;
    instruction_access_in.presponse.pbits.pword = instruction_response_bits;
    instruction_access_in.presponse.pbits.ppage_ufault = UINT64_C(0);
    instruction_access_in.presponse.pbits.paccess_ufault = UINT64_C(0);
    data_access_in.prequest.pready = UINT64_C(1);
    data_access_in.prequest_ufault = UINT64_C(0);
    data_access_in.prequest_uaccess_ufault =
        data_access_out.prequest.pvalid &&
        data_access_out.prequest.pbits.paccess == MEMORY_LOAD &&
        data_access_out.prequest.pbits.paddress == UINT64_C(0);
    data_access_in.presponse = {};
    data_access_in.pdrained = UINT64_C(1);
    data_access_in.preservation_uvalid = UINT64_C(0);
  }
}

void observe() {
  {
    if (reset) {
      defer(instruction_response_valid, UINT64_C(0));
      defer(instruction_response_bits,
            std::remove_cvref_t<decltype(instruction_response_bits)>{});
      defer(faulting_load_seen, UINT64_C(0));
      defer(stores_seen, std::remove_cvref_t<decltype(stores_seen)>{});
    } else {
      if (instruction_access_out.pflush ||
          (instruction_response_valid &&
           instruction_access_out.presponse.pready))
        defer(instruction_response_valid, UINT64_C(0));
      if (instruction_access_out.prequest.pvalid &&
          instruction_access_in.prequest.pready) {
        defer(instruction_response_valid, UINT64_C(1));
        defer(instruction_response_bits,
              instruction_at(instruction_access_out.prequest.pbits.paddress));
      }

      if (data_access_out.prequest.pvalid && data_access_in.prequest.pready) {
        if (data_access_out.prequest.pbits.paccess == MEMORY_LOAD) {
          CHECK(!faulting_load_seen && data_access_in.prequest_uaccess_ufault &&
                data_access_out.prequest.pbits.paddress == UINT64_C(0) &&
                memory_rd(data_access_out.prequest.pbits.pcontext.pwriteback) ==
                    UINT64_C(5));
          defer(faulting_load_seen, UINT64_C(1));
        } else {
          CHECK(data_access_out.prequest.pbits.paccess == MEMORY_STORE &&
                data_access_out.prequest.pbits.paddress != UINT64_C(32));
          if (stores_seen == 0) {
            CHECK(faulting_load_seen &&
                  data_access_out.prequest.pbits.paddress == UINT64_C(24) &&
                  data_access_out.prequest.pbits.pdata == UINT64_C(9));
            defer(stores_seen, 1);
          } else if (stores_seen == 1) {
            CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(8) &&
                  data_access_out.prequest.pbits.pdata == UINT64_C(5));
            defer(stores_seen, 2);
          } else {
            CHECK(stores_seen == 2 &&
                  data_access_out.prequest.pbits.paddress == UINT64_C(16) &&
                  data_access_out.prequest.pbits.pdata == UINT64_C(0));

            throw Finished{};
          }
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

    for (int repeat_index = 0; repeat_index < (2); ++repeat_index)
      rising();
    settle();
    reset = UINT64_C(0);
    rising();
    settle();
    for (int repeat_index = 0; repeat_index < (120); ++repeat_index)
      rising();
    fail(1, "data access fault handler did not complete");
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
