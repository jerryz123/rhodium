// Preserves the rv5stage-zcb cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
// Executes a short mixed-width Zcb program through RV5Stage's normal pipeline.

bool instruction_response_valid;

std::uint32_t instruction_response_bits;
constexpr std::uint8_t MEMORY_STORE = UINT64_C(2);
constexpr std::uint8_t MEMORY_WIDTH_BYTE = UINT64_C(0);
constexpr std::uint8_t WRITEBACK_ACK_KIND = UINT64_C(0);

std::uint32_t instruction_at(std::uint64_t address) {
  switch (address) {
  case UINT64_C(0x100000000): {
    return UINT64_C(0x4495440d); // c.li x8, 3; c.li x9, 5
  } break;
  case UINT64_C(0x100000004): {
    return UINT64_C(0x45019c45); // c.mul x8, x9; c.li x10, 0
  } break;
  case UINT64_C(0x100000008): {
    return UINT64_C(0x18900); // c.sb x8, 0(x10); c.nop
  } break;
  default: {
    return UINT64_C(0x10001); // c.nop; c.nop
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
    }

    if (!reset && data_access_out.prequest.pvalid &&
        data_access_in.prequest.pready) {
      CHECK(data_access_out.prequest.pbits.paccess == MEMORY_STORE &&
            data_access_out.prequest.pbits.pwidth == MEMORY_WIDTH_BYTE &&
            data_access_out.prequest.pbits.paddress == UINT64_C(0) &&
            data_access_out.prequest.pbits.pdata == UINT64_C(15) &&
            bit_slice(data_access_out.prequest.pbits.pcontext.pwriteback, 7,
                      2) == WRITEBACK_ACK_KIND);

      throw Finished{};
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
    for (int repeat_index = 0; repeat_index < (300); ++repeat_index)
      rising();
    fail(1, "Zcb program did not reach its byte store");
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
