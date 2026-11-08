// Preserves the rv5stage-branch-prediction cycle and architectural oracle through direct rsim.
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
// Compares real loop/indirect-branch execution, wrong-path stores, flushes, and cycles with BTB off/on.

instruction_resp_t response[2];

int cycles = 0, completed[2], stores[2], flushes[2], warm_pairs[2],
    last_request_cycle[2];

std::uint64_t last_request[2];
bool alias_mode = false, benchmark_finished = false;
int alias_requests[2]{}, fault_requests[2]{};
int training_stores[2]{};
constexpr auto invalid_target = (UINT64_C(1) << 45) + 0x100;

std::uint32_t branch(int offset) {
  std::uint16_t immediate;
  immediate = ((offset)&low_mask(13));
  return (field(bit_slice(immediate, 12, 1), 1, 31) |
          field(bit_slice(immediate, 5, 6), 6, 25) | field(UINT64_C(0), 5, 20) |
          field(UINT64_C(1), 5, 15) | field(UINT64_C(1), 3, 12) |
          field(bit_slice(immediate, 1, 4), 4, 8) |
          field(bit_slice(immediate, 11, 1), 1, 7) | field(UINT64_C(99), 7, 0));
}
std::uint32_t jump(int offset) {
  std::uint32_t immediate;
  immediate = ((offset)&low_mask(21));
  return (field(bit_slice(immediate, 20, 1), 1, 31) |
          field(bit_slice(immediate, 1, 10), 10, 21) |
          field(bit_slice(immediate, 11, 1), 1, 20) |
          field(bit_slice(immediate, 12, 8), 8, 12) | field(UINT64_C(0), 5, 7) |
          field(UINT64_C(111), 7, 0));
}
std::uint32_t instruction_at(std::uint64_t address) {
  if (alias_mode) {
    switch (address) {
    case 0: return 0x20000513; // li x10,0x200
    case 4: return 0x30551073; // csrw mtvec,x10
    case 8: return 0x10000193; // li x3,0x100; first visit trains the valid alias
    case 12:
    case 16: return 19;
    case 20: return 0x000182e7; // jalr x5,x3,0; link must retire even when the target fails
    case 24: return 0x18003023; // Fallthrough poison: must never issue.
    case 0x100: return 0x10003023; // Allowed on training visit only; poison on replay.
    case 0x104: return 0x00100193; // li x3,1
    case 0x108: return 0x02d19193; // slli x3,x3,45
    case 0x10c: return 0x10018193; // addi x3,x3,0x100: change only discarded high bits
    case 0x110: return jump(20 - 0x110);
    case 0x200: return 0x34302273; // csrr x4,mtval
    case 0x204: return 0x00403023; // sd x4,0(x0)
    case 0x208: return 0x34102273; // csrr x4,mepc
    case 0x20c: return 0x00403423; // sd x4,8(x0)
    case 0x210: return 0x34202273; // csrr x4,mcause
    case 0x214: return 0x00403823; // sd x4,16(x0)
    case 0x218: return 0x00503c23; // sd x5,24(x0): committed link is 24
    case 0x21c: return jump(0);
    default: return 19;
    }
  }
  switch (address) {
  case 0: {
    return UINT64_C(0x1400093); // x1 = 20
  } break;
  case 4: {
    return UINT64_C(275); // x2 = 0
  } break;
  case 8: {
    return UINT64_C(0x110113); // x2++
  } break;
  case 12: {
    return UINT64_C(0xfff08093); // x1--
  } break;
  case 16: {
    return branch(-8);
  } break;
  case 20: {
    return UINT64_C(0x203023); // sd x2, 0(x0)
  } break;
  case 24: {
    return UINT64_C(0x2800193); // x3 = 40
  } break;
  case 28: {
    return UINT64_C(0x18267); // jalr x4, 0(x3), revisited with three targets
  } break;
  case 32:
  case 52:
  case 68: {
    return UINT64_C(0x10003023); // Wrong-path store, must never issue.
  } break;
  case 40: {
    return UINT64_C(0x3800193); // x3 = 56
  } break;
  case 44: {
    return UINT64_C(19);
  } break;
  case 48: {
    return jump(-20);
  } break;
  case 56: {
    return UINT64_C(0x403423); // sd x4, 8(x0), link must be 32
  } break;
  case 60: {
    return UINT64_C(0x4800193); // x3 = 72
  } break;
  case 64: {
    return jump(-36);
  } break;
  case 72: {
    return UINT64_C(0x203823); // sd x2, 16(x0)
  } break;
  case 76: {
    return UINT64_C(4111); // fence.i
  } break;
  case 80: {
    return UINT64_C(0x203c23); // sd x2, 24(x0)
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
        stores[i] = 0;
        flushes[i] = 0;
        warm_pairs[i] = 0;
        last_request[i] = UINT64_MAX;
        last_request_cycle[i] = -1;
        alias_requests[i] = 0;
        fault_requests[i] = 0;
        training_stores[i] = 0;
      } else if (completed[i] == 0) {
        if (instruction_out(i).pflush) {
          defer(response[i], std::remove_cvref_t<decltype(response[i])>{});
          flushes[i]++;
          last_request[i] = UINT64_MAX;
        } else if (instruction_out(i).presponse.pready)
          defer(response[i].pvalid, 0);
        if (instruction_out(i).prequest.pvalid &&
            instruction_in(i).prequest.pready) {
          defer(response[i],
                std::remove_cvref_t<decltype(response[i])>{
                    UINT64_C(1),
                    {instruction_at(instruction_out(i).prequest.pbits.paddress),
                     alias_mode && (instruction_out(i).prequest.pbits.paddress >> 45) == 1,
                     UINT64_C(0)}});
          if (alias_mode) {
            alias_requests[i] += instruction_out(i).prequest.pbits.paddress == 0x100;
            fault_requests[i] += instruction_out(i).prequest.pbits.paddress == invalid_target;
          }
          if (last_request[i] == 16 &&
              instruction_out(i).prequest.pbits.paddress == 8 &&
              cycles == last_request_cycle[i] + 1)
            warm_pairs[i]++;
          last_request[i] = instruction_out(i).prequest.pbits.paddress;
          last_request_cycle[i] = cycles;
        }
        if (data_out(i).prequest.pvalid) {
          if (alias_mode && data_out(i).prequest.pbits.paddress == 0x100) {
            CHECK(training_stores[i]++ == 0);
            CHECK(data_out(i).prequest.pbits.pdata == 0);
            continue;
          }
          if (alias_mode) {
            CHECK(data_out(i).prequest.pbits.paddress == (stores[i] % 4) * 8);
            CHECK(data_out(i).prequest.pbits.pdata ==
                  (stores[i] % 4 == 2 ? UINT64_C(1) : stores[i] % 4 == 3 ? UINT64_C(24) : invalid_target));
          } else {
            CHECK(data_out(i).prequest.pbits.paddress ==
                  ((stores[i] * 8) & low_mask(64)));
            CHECK(data_out(i).prequest.pbits.pdata ==
                  (stores[i] == 1 ? UINT64_C(32) : UINT64_C(20)));
          }
          stores[i]++;
          if (stores[i] == 4)
            completed[i] = cycles;
        }
      }
    }
    if (completed[0] != 0 && completed[1] != 0) {
      if (alias_mode) {
        CHECK(alias_requests[0] >= 2 && alias_requests[1] == 1);
        CHECK(fault_requests[0] == 0 && fault_requests[1] == 0);
        CHECK(training_stores[0] == 1 && training_stores[1] == 1);
        throw Finished{};
      }
      CHECK(warm_pairs[0] >= 10 && warm_pairs[1] == 0);
      CHECK(completed[0] < completed[1] && flushes[0] + 10 < flushes[1]);
      benchmark_finished = true;
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
    cycle_limit = 5002;
    try {
      stimulus();
      while (!benchmark_finished)
        rising();
      alias_mode = true;
      stimulus();
      for (;;)
        rising();
    } catch (const Finished &) {
    }
  });
}
