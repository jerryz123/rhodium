// Preserves the rv5stage-sscofpmf-core cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
// Verifies retirement-driven HPM overflow waits for older stores before precise trap entry.

constexpr std::uint8_t MEMORY_STORE = UINT64_C(2);
constexpr std::uint64_t COUNTER_OVERFLOW_CAUSE = UINT64_C(0x800000000000000d);

bool instruction_response_valid;

std::uint32_t instruction_response_bits;

std::uint8_t regular_stores;

bool overflow_caused;

bool handler_mepc_seen;

std::uint16_t cycles;

int store_completion_delay;

std::uint32_t instruction_at(std::uint64_t address) {
  switch (address) {
  case UINT64_C(0): {
    return UINT64_C(0x10000093); // addi x1, x0, 0x100
  } break;
  case UINT64_C(4): {
    return UINT64_C(0x30509073); // csrw mtvec, x1
  } break;
  case UINT64_C(8): {
    return UINT64_C(8375); // lui x1, 2 (LCOFIE)
  } break;
  case UINT64_C(12): {
    return UINT64_C(0x30409073); // csrw mie, x1
  } break;
  case UINT64_C(16): {
    return UINT64_C(0x800093); // addi x1, x0, 8
  } break;
  case UINT64_C(20): {
    return UINT64_C(0x3000a073); // csrs mstatus, x1
  } break;
  case UINT64_C(24): {
    return UINT64_C(0x200093); // addi x1, x0, 2 (retirement event)
  } break;
  case UINT64_C(28): {
    return UINT64_C(0x32309073); // csrw mhpmevent3, x1
  } break;
  case UINT64_C(32): {
    return UINT64_C(0xffe00093); // addi x1, x0, -2
  } break;
  case UINT64_C(36): {
    return UINT64_C(0xb0309073); // csrw mhpmcounter3, x1
  } break;
  case UINT64_C(40): {
    return UINT64_C(0x100113); // addi x2, x0, 1: count becomes -1
  } break;
  case UINT64_C(44): {
    return UINT64_C(0x203023); // sd x2, 0(x0): retirement wraps
  } break;
  case UINT64_C(48): {
    return UINT64_C(0x200113); // addi x2, x0, 2
  } break;
  case UINT64_C(52): {
    return UINT64_C(0x203423); // sd x2, 8(x0)
  } break;
  case UINT64_C(56): {
    return UINT64_C(0x300113); // addi x2, x0, 3
  } break;
  case UINT64_C(60): {
    return UINT64_C(0x203823); // sd x2, 16(x0)
  } break;
  case UINT64_C(256): {
    return UINT64_C(0x341021f3); // csrr x3, mepc
  } break;
  case UINT64_C(260): {
    return UINT64_C(0x303c23); // sd x3, 24(x0)
  } break;
  case UINT64_C(264): {
    return UINT64_C(0x34202273); // csrr x4, mcause
  } break;
  case UINT64_C(268): {
    return UINT64_C(0x2403023); // sd x4, 32(x0)
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
    data_access_in.prequest_uaccess_ufault = UINT64_C(0);
    data_access_in.presponse = {};
    data_access_in.pdrained = store_completion_delay == 0;
    data_access_in.preservation_uvalid = UINT64_C(0);
  }
}

void observe() {
  {
    if (reset) {
      defer(instruction_response_valid, UINT64_C(0));
      defer(instruction_response_bits,
            std::remove_cvref_t<decltype(instruction_response_bits)>{});
      defer(interrupts, std::remove_cvref_t<decltype(interrupts)>{});
      defer(regular_stores, std::remove_cvref_t<decltype(regular_stores)>{});
      defer(overflow_caused, UINT64_C(0));
      defer(handler_mepc_seen, UINT64_C(0));
      defer(cycles, std::remove_cvref_t<decltype(cycles)>{});
      defer(store_completion_delay, 0);
    } else {
      defer(cycles, cycles + UINT64_C(1));
      if (store_completion_delay != 0) {
        defer(store_completion_delay, store_completion_delay - 1);
        CHECK(bit_slice(mstatus, 3, 1));
      }
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
        CHECK(data_access_out.prequest.pbits.paccess == MEMORY_STORE);
        switch (data_access_out.prequest.pbits.paddress) {
        case UINT64_C(0): {
          {
            CHECK(data_access_out.prequest.pbits.pdata == UINT64_C(1));
            defer(bit_slice(regular_stores, 0, 1), UINT64_C(1));
            CHECK(bit_slice(mstatus, 3, 1));
            defer(overflow_caused, UINT64_C(1));
            defer(store_completion_delay, 20);
          }
        } break;
        case UINT64_C(8): {
          {
            CHECK(data_access_out.prequest.pbits.pdata == UINT64_C(2));
            defer(bit_slice(regular_stores, 1, 1), UINT64_C(1));
          }
        } break;
        case UINT64_C(16): {
          {
            CHECK(data_access_out.prequest.pbits.pdata == UINT64_C(3));
            defer(bit_slice(regular_stores, 2, 1), UINT64_C(1));
          }
        } break;
        case UINT64_C(24): {
          {
            CHECK(overflow_caused &&
                  data_access_out.prequest.pbits.pdata >= UINT64_C(48) &&
                  data_access_out.prequest.pbits.pdata <= UINT64_C(64) &&
                  bit_slice(data_access_out.prequest.pbits.pdata, 0, 2) ==
                      UINT64_C(0));
            CHECK(bit_slice(regular_stores, 0, 1));
            CHECK(bit_slice(regular_stores, 1, 1) ==
                  (data_access_out.prequest.pbits.pdata >= UINT64_C(56)));
            CHECK(bit_slice(regular_stores, 2, 1) ==
                  (data_access_out.prequest.pbits.pdata >= UINT64_C(64)));
            defer(handler_mepc_seen, UINT64_C(1));
          }
        } break;
        case UINT64_C(32): {
          {
            CHECK(handler_mepc_seen && data_access_out.prequest.pbits.pdata ==
                                           COUNTER_OVERFLOW_CAUSE);
            CHECK(privilege == UINT64_C(3) && !bit_slice(mstatus, 3, 1));

            throw Finished{};
          }
        } break;
        default: {
          fail(1, "interrupt fixture stored to an unexpected address");
        } break;
        }
      }

      CHECK(cycles != UINT64_C(511));
    }
  }
}

void falling_update() {}

void stimulus() {
  reset = UINT64_C(1);
  time_counter = {};
  hart_id = {};
  {

    for (int repeat_index = 0; repeat_index < (2); ++repeat_index)
      rising();
    settle();
    reset = UINT64_C(0);
    rising();
    settle();
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
