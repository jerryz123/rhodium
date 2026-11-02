// Preserves the rv5stage-access-fault cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
using instruction_resp_bits_t =
    std::remove_cvref_t<decltype(instruction_access_in.presponse.pbits)>;
// Checks immediate and delayed delivery of fetch faults and CSR-generated exceptions.

constexpr std::uint8_t MEMORY_STORE = UINT64_C(2);
constexpr std::uint32_t UnknownCsrRead = 0xcc002673; // csrr x12, 0xcc0
bool csr_fault, drained, fetched_fault_pc;

bool instruction_response_valid;

instruction_resp_bits_t instruction_response_bits;

std::uint8_t stores_seen;

std::uint32_t instruction_at(std::uint64_t address) {
  switch (address) {
  case UINT64_C(0): {
    return UINT64_C(0x342020f3); // csrr x1, mcause
  } break;
  case UINT64_C(4): {
    return UINT64_C(0x103023); // sd x1, 0(x0)
  } break;
  case UINT64_C(8): {
    return UINT64_C(0x34302173); // csrr x2, mtval
  } break;
  case UINT64_C(12): {
    return UINT64_C(0x203423); // sd x2, 8(x0)
  } break;
  case 16: return 0x341021f3; // csrr x3, mepc
  case 20: return 0x303823;   // sd x3, 16(x0)
  case 256: return csr_fault ? UnknownCsrRead : 19;
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
    instruction_access_in.presponse.pbits = instruction_response_bits;
    data_access_in.prequest.pready = UINT64_C(1);
    data_access_in.prequest_ufault = UINT64_C(0);
    data_access_in.prequest_uaccess_ufault = UINT64_C(0);
    data_access_in.presponse = {};
    data_access_in.pdrained = drained;
    data_access_in.preservation_uvalid = UINT64_C(0);
  }
}

void observe() {
  {
    if (reset) {
      defer(instruction_response_valid, UINT64_C(0));
      defer(instruction_response_bits,
            std::remove_cvref_t<decltype(instruction_response_bits)>{});
      defer(stores_seen, std::remove_cvref_t<decltype(stores_seen)>{});
      defer(fetched_fault_pc, false);
    } else {
      if (instruction_access_out.pflush ||
          (instruction_response_valid &&
           instruction_access_out.presponse.pready))
        defer(instruction_response_valid, UINT64_C(0));
      if (instruction_access_out.prequest.pvalid &&
          instruction_access_in.prequest.pready) {
        const auto pc = instruction_access_out.prequest.pbits.paddress;
        if (pc == 256) defer(fetched_fault_pc, true);
        if (pc == 0) CHECK(drained); // No handler fetch before older work drains.
        defer(instruction_response_valid, UINT64_C(1));
        defer(instruction_response_bits.pword,
              instruction_at(instruction_access_out.prequest.pbits.paddress));
        defer(instruction_response_bits.ppage_ufault, UINT64_C(0));
        defer(instruction_response_bits.paccess_ufault,
              !csr_fault && pc == 256);
      }

      if (data_access_out.prequest.pvalid && data_access_in.prequest.pready) {
        CHECK(data_access_out.prequest.pbits.paccess == MEMORY_STORE);
        CHECK(data_access_out.prequest.pbits.paddress != UINT64_C(32));
        if (stores_seen == 0) {
          CHECK(data_access_out.prequest.pbits.paddress == 0 &&
                data_access_out.prequest.pbits.pdata == (csr_fault ? 2u : 1u));
          defer(stores_seen, 1);
        } else if (stores_seen == 1) {
          CHECK(data_access_out.prequest.pbits.paddress == 8 &&
                data_access_out.prequest.pbits.pdata == (csr_fault ? UnknownCsrRead : 256u));
          defer(stores_seen, 2);
        } else {
          CHECK(stores_seen == 2 && data_access_out.prequest.pbits.paddress == 16 &&
                data_access_out.prequest.pbits.pdata == 256);
          defer(stores_seen, 3);
        }
      }
    }
  }
}

void falling_update() {}

void stimulus(bool delayed, bool generated) {
  csr_fault = generated;
  drained = !delayed;
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
    if (delayed) {
      for (int i = 0; i < 40; ++i) rising();
      CHECK(fetched_fault_pc && stores_seen == 0);
      drained = true;
    }
    for (int repeat_index = 0; repeat_index < 80 && stores_seen != 3; ++repeat_index)
      rising();
    CHECK(stores_seen == 3);
  }
}

int main() {
  return run_test([] {
    cycle_limit = 1000000;
    stimulus(false, false);
    stimulus(true, false);
    stimulus(true, true);
  });
}
