// Preserves the rv5stage-return-prediction cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
auto &instruction_in(unsigned i) {
  return i ? instruction_1_in : instruction_0_in;
}
auto &instruction_out(unsigned i) {
  return i ? instruction_1_out : instruction_0_out;
}
auto &data_in(unsigned i) { return i ? data_1_in : data_0_in; }
auto &data_out(unsigned i) { return i ? data_1_out : data_0_out; }
using instruction_resp_t =
    std::remove_cvref_t<decltype(instruction_0_in.presponse)>;
// Compares alternating-call-site return prediction under the same two-entry BTB pressure.

instruction_resp_t response[2];

int cycles = 0, completed[2], flushes[2];

std::uint32_t branch_not_equal(int rs1, int offset) {
  std::uint16_t immediate;
  immediate = ((offset)&low_mask(13));
  return (field(bit_slice(immediate, 12, 1), 1, 31) |
          field(bit_slice(immediate, 5, 6), 6, 25) | field(UINT64_C(0), 5, 20) |
          field(((rs1)&low_mask(5)), 5, 15) | field(UINT64_C(1), 3, 12) |
          field(bit_slice(immediate, 1, 4), 4, 8) |
          field(bit_slice(immediate, 11, 1), 1, 7) | field(UINT64_C(99), 7, 0));
}
std::uint32_t jump_and_link(int rd, int offset) {
  std::uint32_t immediate;
  immediate = ((offset)&low_mask(21));
  return (field(bit_slice(immediate, 20, 1), 1, 31) |
          field(bit_slice(immediate, 1, 10), 10, 21) |
          field(bit_slice(immediate, 11, 1), 1, 20) |
          field(bit_slice(immediate, 12, 8), 8, 12) |
          field(((rd)&low_mask(5)), 5, 7) | field(UINT64_C(111), 7, 0));
}
std::uint32_t instruction_at(std::uint64_t address) {
  switch (address) {
  case 0: {
    return UINT64_C(0x1000113); // x2 = 16 loop iterations.
  } break;
  case 4: {
    return UINT64_C(403); // x3 = 0 completed calls.
  } break;
  case 8: {
    return jump_and_link(1, 32); // Call function, return to 12.
  } break;
  case 12: {
    return jump_and_link(1, 28); // Same function, return to 16.
  } break;
  case 16: {
    return UINT64_C(0xfff10113); // x2--.
  } break;
  case 20: {
    return branch_not_equal(2, -12); // Repeat at the first call site.
  } break;
  case 24: {
    return UINT64_C(0x303023); // sd x3, 0(x0): completion.
  } break;
  case 40: {
    return UINT64_C(0x118193); // Function body: x3++.
  } break;
  case 44: {
    return UINT64_C(32871); // ret: jalr x0, 0(x1).
  } break;
  default: {
    return UINT64_C(19);
  } break;
  }
}

void drive() {
  {
    for (int i = 0; i < 2; i++) {
      instruction_in(i) = {};
      instruction_in(i).prequest.pready = instruction_out(i).pflush ||
                                          !response[i].pvalid ||
                                          instruction_out(i).presponse.pready;
      instruction_in(i).presponse = response[i];
      data_in(i) = {};
      data_in(i).prequest.pready = 1;
      data_in(i).pdrained = 1;
    }
  }
}

void observe() {
  {
    cycles = cycles + 1;
    for (int i = 0; i < 2; i++) {
      if (reset) {
        defer(response[i], std::remove_cvref_t<decltype(response[i])>{});
        completed[i] = 0;
        flushes[i] = 0;
      } else if (completed[i] == 0) {
        if (instruction_out(i).pflush) {
          defer(response[i], std::remove_cvref_t<decltype(response[i])>{});
          flushes[i]++;
        } else if (instruction_out(i).presponse.pready) {
          defer(response[i].pvalid, 0);
        }
        if (instruction_out(i).prequest.pvalid &&
            instruction_in(i).prequest.pready)
          defer(response[i],
                std::remove_cvref_t<decltype(response[i])>{
                    UINT64_C(1),
                    {instruction_at(instruction_out(i).prequest.pbits.paddress),
                     UINT64_C(0), UINT64_C(0)}});
        if (data_out(i).prequest.pvalid) {
          CHECK(data_out(i).prequest.pbits.paddress == 0 &&
                data_out(i).prequest.pbits.pdata == 32);
          completed[i] = cycles;
        }
      }
    }
    if (completed[0] != 0 && completed[1] != 0) {
      CHECK(completed[0] + 20 < completed[1]);
      CHECK(flushes[0] + 20 < flushes[1]);

      throw Finished{};
    }
  }
}

void falling_update() {}

void stimulus() {
  reset = 1;
  {
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      falling();
    reset = 0;
  }
}

int main() {
  return run_test([] {
    cycle_limit = 10002;
    try {
      stimulus();
      for (;;)
        rising();
    } catch (const Finished &) {
    }
  });
}
