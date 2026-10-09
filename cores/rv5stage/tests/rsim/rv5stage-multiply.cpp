// Preserves the rv5stage-multiply cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
extern "C" void multiply_trace_bind();
extern "C" void multiply_trace_finish();
// Verifies direct EX multiplication, consecutive launches, three-cycle dependencies, and replay cancellation.

bool instruction_response_valid;

std::uint32_t instruction_response_bits;

std::uint16_t cycles;

std::uint8_t stores_seen;

bool rejected_store = 0;
constexpr std::uint8_t MEMORY_STORE = UINT64_C(2);

std::uint32_t instruction_at(std::uint64_t address) {
  switch (address) {
  case UINT64_C(4294967296): {
    return UINT64_C(6292115); // addi x5, x0, 6
  } break;
  case UINT64_C(4294967300): {
    return UINT64_C(4287628051); // addi x6, x0, -7
  } break;
  case UINT64_C(4294967304): {
    return UINT64_C(40010675); // mul x7, x5, x6
  } break;
  case UINT64_C(4294967308): {
    return UINT64_C(9438227); // addi x8, x0, 9
  } break;
  case UINT64_C(4294967312): {
    return UINT64_C(41955363); // sd x8, 32(x0)
  } break;
  case UINT64_C(4294967316): {
    return UINT64_C(8619187); // add x9, x7, x8
  } break;
  case UINT64_C(4294967320): {
    return UINT64_C(42120499); // mulhu x10, x5, x8
  } break;
  case UINT64_C(4294967324): {
    return UINT64_C(42141115); // mulw x11, x6, x8
  } break;
  case UINT64_C(4294967328): {
    return UINT64_C(42145331); // mulh x12, x6, x8
  } break;
  case UINT64_C(4294967332): {
    return UINT64_C(42149555); // mulhsu x13, x6, x8
  } break;
  case UINT64_C(4294967336): {
    return UINT64_C(7352355); // sd x7, 0(x0)
  } break;
  case UINT64_C(4294967340): {
    return UINT64_C(9450531); // sd x9, 8(x0)
  } break;
  case UINT64_C(4294967344): {
    return UINT64_C(10500131); // sd x10, 16(x0)
  } break;
  case UINT64_C(4294967348): {
    return UINT64_C(11549731); // sd x11, 24(x0)
  } break;
  case UINT64_C(4294967352): {
    return UINT64_C(46150691); // sd x12, 40(x0)
  } break;
  case UINT64_C(4294967356): {
    return UINT64_C(47200291); // sd x13, 48(x0)
  } break;
  case UINT64_C(4294967360): {
    return UINT64_C(1050387); // addi x14, x0, 1
  } break;
  case UINT64_C(4294967364): {
    return UINT64_C(15139939); // beq x14, x14, +8
  } break;
  case UINT64_C(4294967368): {
    return UINT64_C(40011699); // wrong-path mul x15, x5, x6
  } break;
  case UINT64_C(4294967396): {
    return UINT64_C(49298467); // sd x15, 56(x0)
  } break;
  case UINT64_C(4294967400): {
    return UINT64_C(40011955); // mul x17, x5, x6
  } break;
  case UINT64_C(4294967404): {
    return UINT64_C(8948019); // add x18, x17, x8
  } break;
  case UINT64_C(4294967408): {
    return UINT64_C(85995555); // sd x18, 64(x0)
  } break;
  case UINT64_C(4294967412): {
    return UINT64_C(75510819); // sd x8, 72(x0), replay once
  } break;
  case UINT64_C(4294967416): {
    return UINT64_C(40012211); // mul x19, x5, x6, killed in MEM then retried
  } break;
  case UINT64_C(4294967420): {
    return UINT64_C(87046179); // sd x19, 80(x0)
  } break;
  default: {
    return UINT64_C(19);
  } break;
  }
}

void drive() {
  {
    instruction_access_in.prequest.pready =
        instruction_access_out.pflush || !instruction_response_valid ||
        instruction_access_out.presponse.pready;
    instruction_access_in.presponse.pvalid = instruction_response_valid;
    instruction_access_in.presponse.pbits.pword = instruction_response_bits;
    instruction_access_in.presponse.pbits.ppage_ufault = UINT64_C(0);
    instruction_access_in.presponse.pbits.paccess_ufault = UINT64_C(0);
    data_access_in.prequest.pready =
        rejected_store ||
        data_access_out.prequest.pbits.paddress != UINT64_C(72);
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
      defer(cycles, std::remove_cvref_t<decltype(cycles)>{});
      defer(stores_seen, std::remove_cvref_t<decltype(stores_seen)>{});
      defer(rejected_store, 0);
    } else {
      defer(cycles, cycles + UINT64_C(1));
      if (data_access_out.prequest.pvalid && !data_access_in.prequest.pready)
        defer(rejected_store, 1);
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
        switch (stores_seen) {
        case 0: {
          {
            CHECK(cycles < 40 &&
                  data_access_out.prequest.pbits.paddress == UINT64_C(32) &&
                  data_access_out.prequest.pbits.pdata == UINT64_C(9));
          }
        } break;
        case 1: {
          {
            CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(0) &&
                  data_access_out.prequest.pbits.pdata ==
                      UINT64_C(18446744073709551574));
          }
        } break;
        case 2: {
          {
            CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(8) &&
                  data_access_out.prequest.pbits.pdata ==
                      UINT64_C(18446744073709551583));
          }
        } break;
        case 3: {
          {
            CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(16) &&
                  data_access_out.prequest.pbits.pdata == UINT64_C(0));
          }
        } break;
        case 4: {
          {
            CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(24) &&
                  data_access_out.prequest.pbits.pdata ==
                      UINT64_C(18446744073709551553));
          }
        } break;
        case 5: {
          {
            CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(40) &&
                  data_access_out.prequest.pbits.pdata ==
                      UINT64_C(18446744073709551615));
          }
        } break;
        case 6: {
          {
            CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(48) &&
                  data_access_out.prequest.pbits.pdata ==
                      UINT64_C(18446744073709551615));
          }
        } break;
        case 7: {
          {
            CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(56) &&
                  data_access_out.prequest.pbits.pdata == UINT64_C(0));
          }
        } break;
        case 8: {
          {
            CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(64) &&
                  data_access_out.prequest.pbits.pdata ==
                      UINT64_C(18446744073709551583));
          }
        } break;
        case 9: {
          {
            CHECK(rejected_store &&
                  data_access_out.prequest.pbits.paddress == UINT64_C(72) &&
                  data_access_out.prequest.pbits.pdata == UINT64_C(9));
          }
        } break;
        case 10: {
          {
            CHECK(data_access_out.prequest.pbits.paddress == UINT64_C(80) &&
                  data_access_out.prequest.pbits.pdata ==
                      UINT64_C(18446744073709551574));
            multiply_trace_finish();
            ;
            throw Finished{};
          }
        } break;
        default: {
          fail(1, "unexpected extra store");
        } break;
        }
        defer(stores_seen, stores_seen + UINT64_C(1));
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
    multiply_trace_bind();
    interrupts = {};

    for (int repeat_index = 0; repeat_index < (2); ++repeat_index)
      rising();
    settle();
    reset = UINT64_C(0);
    rising();
    settle();
    for (int repeat_index = 0; repeat_index < (600); ++repeat_index)
      rising();
    fail(1, "core did not complete the multiply scenario: stores=%0d fetch=%h",
         stores_seen, instruction_access_out.prequest.pbits.paddress);
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
