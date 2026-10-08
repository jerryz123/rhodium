// Preserves the rv5stage-wfi cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
// Verifies RV5Stage WFI sleep, masked wake, lost-wakeup avoidance, and interrupt entry.

constexpr std::uint8_t MEMORY_STORE = UINT64_C(2);
constexpr std::uint64_t MACHINE_TIMER_CAUSE = UINT64_C(0x8000000000000007);
constexpr std::uint8_t SETUP_MASKED_WAIT = UINT64_C(0);
constexpr std::uint8_t MASKED_WAIT = UINT64_C(1);
constexpr std::uint8_t MASKED_RESUME = UINT64_C(2);
constexpr std::uint8_t PENDING_BEFORE_WFI = UINT64_C(3);
constexpr std::uint8_t SETUP_TRAPPING_WAIT = UINT64_C(4);
constexpr std::uint8_t TRAPPING_WAIT = UINT64_C(5);
constexpr std::uint8_t INTERRUPT_HANDLER = UINT64_C(6);

bool instruction_response_valid;

std::uint32_t instruction_response_bits;

std::uint8_t phase;

std::uint8_t quiet_cycles;

bool handler_mepc_seen;

std::uint16_t cycles;

std::uint32_t instruction_at(std::uint64_t address) {
  switch (address) {
  case UINT64_C(0): {
    return UINT64_C(0x10000093); // addi x1, x0, 0x100
  } break;
  case UINT64_C(4): {
    return UINT64_C(0x30509073); // csrw mtvec, x1
  } break;
  case UINT64_C(8): {
    return UINT64_C(0x8000093); // addi x1, x0, 0x80
  } break;
  case UINT64_C(12): {
    return UINT64_C(0x30409073); // csrw mie, x1
  } break;
  case UINT64_C(16): {
    return UINT64_C(0x5500113); // addi x2, x0, 0x55
  } break;
  case UINT64_C(20): {
    return UINT64_C(0x2203423); // sd x2, 40(x0)
  } break;
  case UINT64_C(24): {
    return UINT64_C(0x10500073); // wfi
  } break;
  case UINT64_C(28): {
    return UINT64_C(0x100113); // addi x2, x0, 1
  } break;
  case UINT64_C(32): {
    return UINT64_C(0x203023); // sd x2, 0(x0)
  } break;
  case UINT64_C(36): {
    return UINT64_C(0x10500073); // wfi with MTIP already set
  } break;
  case UINT64_C(40): {
    return UINT64_C(0x200113); // addi x2, x0, 2
  } break;
  case UINT64_C(44): {
    return UINT64_C(0x203423); // sd x2, 8(x0)
  } break;
  case UINT64_C(48): {
    return UINT64_C(0x800093); // addi x1, x0, 8
  } break;
  case UINT64_C(52): {
    return UINT64_C(0x3000a073); // csrs mstatus, x1
  } break;
  case UINT64_C(56): {
    return UINT64_C(0x6600113); // addi x2, x0, 0x66
  } break;
  case UINT64_C(60): {
    return UINT64_C(0x2203823); // sd x2, 48(x0)
  } break;
  case UINT64_C(64): {
    return UINT64_C(0x10500073); // wfi
  } break;
  case UINT64_C(68): {
    return UINT64_C(0x300113); // addi x2, x0, 3
  } break;
  case UINT64_C(72): {
    return UINT64_C(0x2203023); // sd x2, 32(x0), must be squashed
  } break;
  case UINT64_C(256): {
    return UINT64_C(0x341021f3); // csrr x3, mepc
  } break;
  case UINT64_C(260): {
    return UINT64_C(0x303823); // sd x3, 16(x0)
  } break;
  case UINT64_C(264): {
    return UINT64_C(0x34202273); // csrr x4, mcause
  } break;
  case UINT64_C(268): {
    return UINT64_C(0x403c23); // sd x4, 24(x0)
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
      defer(interrupts, std::remove_cvref_t<decltype(interrupts)>{});
      defer(phase, SETUP_MASKED_WAIT);
      defer(quiet_cycles, std::remove_cvref_t<decltype(quiet_cycles)>{});
      defer(handler_mepc_seen, UINT64_C(0));
      defer(cycles, std::remove_cvref_t<decltype(cycles)>{});
    } else {
      defer(cycles, cycles + UINT64_C(1));
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

      if (phase == MASKED_WAIT || phase == TRAPPING_WAIT) {
        if (instruction_access_out.prequest.pvalid &&
            instruction_access_in.prequest.pready)
          defer(quiet_cycles, std::remove_cvref_t<decltype(quiet_cycles)>{});
        else
          defer(quiet_cycles, quiet_cycles + UINT64_C(1));
      } else {
        defer(quiet_cycles, std::remove_cvref_t<decltype(quiet_cycles)>{});
      }

      if (phase == MASKED_WAIT && quiet_cycles == UINT64_C(12)) {
        defer(interrupts.pmachine_utimer, UINT64_C(1));
        defer(phase, MASKED_RESUME);
      } else if (phase == TRAPPING_WAIT && quiet_cycles == UINT64_C(12)) {
        defer(interrupts.pmachine_utimer, UINT64_C(1));
        defer(phase, INTERRUPT_HANDLER);
      }

      if (data_access_out.prequest.pvalid && data_access_in.prequest.pready) {
        CHECK(data_access_out.prequest.pbits.paccess == MEMORY_STORE);
        switch (data_access_out.prequest.pbits.paddress) {
        case UINT64_C(40): {
          {
            CHECK(phase == SETUP_MASKED_WAIT &&
                  data_access_out.prequest.pbits.pdata == UINT64_C(85));
            defer(phase, MASKED_WAIT);
          }
        } break;
        case UINT64_C(0): {
          {
            CHECK(phase == MASKED_RESUME &&
                  data_access_out.prequest.pbits.pdata == UINT64_C(1));
            defer(phase, PENDING_BEFORE_WFI);
          }
        } break;
        case UINT64_C(8): {
          {
            CHECK(phase == PENDING_BEFORE_WFI &&
                  data_access_out.prequest.pbits.pdata == UINT64_C(2));
            defer(interrupts.pmachine_utimer, UINT64_C(0));
            defer(phase, SETUP_TRAPPING_WAIT);
          }
        } break;
        case UINT64_C(48): {
          {
            CHECK(phase == SETUP_TRAPPING_WAIT &&
                  data_access_out.prequest.pbits.pdata == UINT64_C(102));
            defer(phase, TRAPPING_WAIT);
          }
        } break;
        case UINT64_C(16): {
          {
            CHECK(phase == INTERRUPT_HANDLER &&
                  data_access_out.prequest.pbits.pdata == UINT64_C(68));
            defer(handler_mepc_seen, UINT64_C(1));
          }
        } break;
        case UINT64_C(24): {
          {
            CHECK(phase == INTERRUPT_HANDLER && handler_mepc_seen &&
                  data_access_out.prequest.pbits.pdata == MACHINE_TIMER_CAUSE);

            throw Finished{};
          }
        } break;
        case UINT64_C(32): {
          fail(1, "instruction after trapping WFI executed before the handler");
        } break;
        default: {
          fail(1, "WFI fixture stored to an unexpected address");
        } break;
        }
      }

      CHECK(!(phase == MASKED_WAIT && data_access_out.prequest.pvalid));
      CHECK(!(phase == TRAPPING_WAIT && data_access_out.prequest.pvalid));

      CHECK(cycles != UINT64_C(1023));
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
