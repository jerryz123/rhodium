// Checks frontend prediction, retained packet ownership, and precise recovery timing.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
using Instruction =
    std::remove_cvref_t<decltype(instructions_out.pbits.pentries[0])>;
using Packet = std::remove_cvref_t<decltype(instructions_out.pbits)>;
int scenario = 0, cycles = 0, source_cycle = -1, accepted_cycle = -1,
    target_cycle = -1, checks = 0, packets = 0;
bool response_valid = 0, response_error = 0, response_replay = 0,
     array_valid = 0, inject_younger_error = 0, inject_younger_replay = 0;
std::uint64_t response_data = 0, array_address = 0, source_pc = 0,
              branch_pc = 0, target_pc = 0;
void falling_update() {}
void drive() {
  translation_in = {translation_out.prequest.pbits, {0, 0, 0, {}}, 0};
  memory_in = {{std::uint8_t(response_valid),
                {response_data, std::uint8_t(response_error),
                 std::uint8_t(response_replay)}}};
}
std::uint32_t jal(int offset) {
  return (field(((offset >> 20) & low_mask(1)), 1, 31) |
          field(((offset >> 1) & low_mask(10)), 10, 21) |
          field(((offset >> 11) & low_mask(1)), 1, 20) |
          field(((offset >> 12) & low_mask(8)), 8, 12) |
          field(UINT64_C(0), 5, 7) | field(UINT64_C(0x6f), 7, 0));
}
std::uint64_t word(std::uint64_t address) {
  if (address == UINT64_C(0x100))
    switch (scenario) {
    case 0:
    case 3:
    case 4:
    case 5:
    case 6:
    case 9:
    case 10:
    case 11:
    case 14:
    case 15: {
      return (field(UINT64_C(0x100013), 32, 32) |
              field(jal(UINT64_C(0x200)), 32, 0));
    } break;
    case 1: {
      return (field(jal(UINT64_C(0x1fc)), 32, 32) |
              field(UINT64_C(0x100013), 32, 0));
    } break;
    case 2: {
      return (field(UINT64_C(0x100013), 32, 32) |
              field(UINT64_C(0x20000063), 32, 0)); // BEQ x0,x0,+512.
    } break;
    case 7: {
      return (field(UINT64_C(0x100010001), 48, 16) |
              field(UINT64_C(0xa001), 16, 0)); // C.J +0.
    } break;
    case 8: {
      return (field(jal(-128), 32, 32) |
              field(UINT64_C(0x100013), 32, 0));
    } break;
    case 12:
    case 13: {
      return (field(UINT64_C(0x6f), 16, 48) |
              field(UINT64_C(0x100010001), 48,
                    0)); // JAL x0,+506 prefix at byte six.
    } break;
    default: {
      return (field(UINT64_C(0x100013), 32, 32) |
              field(UINT64_C(0x100013), 32, 0));
    } break;
    }
  if (address == UINT64_C(0x108) && (scenario == 12 || scenario == 13))
    return (field(UINT64_C(0x100010001), 48, 16) |
            field(UINT64_C(0x1fa0), 16, 0));
  return (field(UINT64_C(0x100013), 32, 32) | field(UINT64_C(0x100013), 32, 0));
}
void observe() {
  if (reset) {
    cycles = 0;
    defer(array_valid, 0);
    defer(response_valid, 0);
  } else {
    cycles++;
    if (virtual_lookup_out.pvalid && virtual_lookup_in.pready &&
        virtual_lookup_out.pbits == UINT64_C(0x100) && source_cycle < 0)
      source_cycle = cycles;
    if (memory_out.prequest.pvalid && !memory_out.ps1_ukill &&
        !memory_out.pflush)
      CHECK(array_valid && memory_out.prequest.pbits.paddress == array_address);
    if (memory_out.prequest.pvalid &&
        memory_out.prequest.pbits.paddress == UINT64_C(0x100) &&
        (scenario == 2 || scenario == 3 || scenario == 7 || scenario == 11 ||
         scenario == 14 || scenario == 15) &&
        accepted_cycle < 0)
      CHECK(cycles == source_cycle + 1 && virtual_lookup_out.pvalid &&
            virtual_lookup_out.pbits == ((scenario == 11 || scenario >= 14)
                                             ? target_pc
                                             : UINT64_C(0x700)));
    defer(array_valid, virtual_lookup_out.pvalid && virtual_lookup_in.pready);
    defer(array_address, virtual_lookup_out.pbits);
    defer(response_valid, memory_out.prequest.pvalid && !memory_out.ps1_ukill &&
                              !memory_out.pflush);
    defer(response_data, word(memory_out.prequest.pbits.paddress));
    defer(response_error,
          inject_younger_error &&
              memory_out.prequest.pbits.paddress != UINT64_C(0x100));
    defer(response_replay,
          inject_younger_replay &&
              memory_out.prequest.pbits.paddress != UINT64_C(0x100));
    if (virtual_lookup_out.pvalid && virtual_lookup_in.pready &&
        virtual_lookup_out.pbits == (target_pc & ~UINT64_C(7)) &&
        cycles > source_cycle && target_cycle < 0)
      target_cycle = cycles;
    if (instructions_out.pvalid && instructions_in.pready) {
      packets++;
      for (int lane = 0; lane < int(instructions_out.pbits.pcount); lane++) {
        Instruction insn;
        insn = instructions_out.pbits.pentries[lane];
        if (insn.ppc == branch_pc && accepted_cycle < 0) {
          accepted_cycle = cycles;
          CHECK(insn.pprediction.pvalid &&
                insn.pprediction.ptarget == target_pc && !insn.pfault.pvalid &&
                int(instructions_out.pbits.pcount) == lane + 1);
          CHECK(memory_out.ps1_ukill == (scenario != 11 && scenario < 14) &&
                !memory_out.pflush);
        }
        if (accepted_cycle >= 0 && cycles > accepted_cycle &&
            insn.ppc != target_pc && insn.ppc < UINT64_C(0x300) &&
            scenario != 7 && scenario != 8)
          fail(1, "younger suffix escaped correction: %h", insn.ppc);
      }
    }
  }
}
void restart(int test_case, std::uint8_t predicted = 0) {
  falling();
  reset = 1;
  start_in = {};
  redirect_in = {};
  branch_update_in = {};
  instructions_in.pready = 1;
  virtual_lookup_in.pready = 1;
  inject_younger_error = 0;
  inject_younger_replay = 0;
  source_cycle = -1;
  accepted_cycle = -1;
  target_cycle = -1;
  packets = 0;
  scenario = test_case;
  source_pc = UINT64_C(0x100);
  branch_pc =
      (test_case == 1 || test_case == 8) ? UINT64_C(0x104) : UINT64_C(0x100);
  if (test_case == 12 || test_case == 13)
    branch_pc = UINT64_C(0x106);
  target_pc = test_case == 7
                  ? UINT64_C(0x100)
                  : (test_case == 8 ? UINT64_C(0x84) : UINT64_C(0x300));
  for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
    falling();
  reset = 0;
  if (predicted || test_case == 15)
    virtual_lookup_in.pready = 0;
  start_in = {UINT64_C(1), source_pc};
  falling();
  start_in = {};
  if (predicted) {
    // Deliberately stale BTB target; S2 must retain its taken direction but
    // replace this target with the instruction's immediate. Train after
    // start's predictor clear, while array admission holds the cursor.
    branch_update_in = {
        UINT64_C(1),
        {branch_pc,
         (test_case == 11 || test_case == 14) ? target_pc : UINT64_C(0x700),
         UINT64_C(1), test_case == 2, UINT64_C(1), test_case == 7, UINT64_C(0),
         UINT64_C(0), branch_pc + (test_case == 7 ? 2 : 4)}};
    falling();
    branch_update_in = {};
    virtual_lookup_in.pready = 1;
  } else if (test_case == 15) {
    // Train at the S0 admission edge. Only an S1 lookup can see this entry
    // and offer its successor before the next edge; an S0 lookup is too early.
    virtual_lookup_in.pready = 1;
    branch_update_in = {UINT64_C(1),
                        {branch_pc, target_pc, UINT64_C(1), UINT64_C(0),
                         UINT64_C(1), UINT64_C(0), UINT64_C(0), UINT64_C(0),
                         branch_pc + 4}};
    falling();
    branch_update_in = {};
  }
}
void check_repair_request() {
  until([&] { return accepted_cycle >= 0; });
  falling();
  CHECK(target_cycle == accepted_cycle && !memory_out.pflush);
  checks++;
}
int main() {
  return run_test([] {
    reset = 1;
    instructions_in.pready = 1;
    virtual_lookup_in.pready = 1;
    for (int test_case = 0; test_case < 3; test_case++) {
      restart(test_case, test_case == 2);
      check_repair_request();
    }
    restart(3, 1);
    check_repair_request();
    restart(4);
    instructions_in.pready = 0;
    inject_younger_error = 1;
    for (int repeat_index = 0; repeat_index < (8); ++repeat_index)
      falling();
    CHECK(accepted_cycle < 0 && packets == 0);
    // The branch is now in returned-block storage. Its correction must outrank
    // the younger S2 error and restart stopped fetch without publishing that error.
    for (int repeat_index = 0; repeat_index < (2); ++repeat_index)
      falling();
    instructions_in.pready = 1;
    check_repair_request();
    restart(5);
    instructions_in.pready = 0;
    until([&] { return instructions_out.pvalid; });
    falling();
    virtual_lookup_in.pready = 0;
    instructions_in.pready = 1;
    until([&] { return accepted_cycle >= 0; });
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
      falling();
      CHECK(virtual_lookup_out.pvalid &&
            virtual_lookup_out.pbits == (target_pc & ~UINT64_C(7)) &&
            target_cycle < 0);
    }
    virtual_lookup_in.pready = 1;
    falling();
    checks++;
    restart(6);
    until([&] { return instructions_out.pvalid; });
    falling();
    redirect_in = {UINT64_C(1),
                   {UINT64_C(0x100),
                    UINT64_C(0x600),
                    {UINT64_C(0), UINT64_C(0), UINT64_C(0), {}}}};
    settle();
    CHECK(accepted_cycle < 0 && virtual_lookup_out.pvalid &&
          virtual_lookup_out.pbits == UINT64_C(0x600));
    falling();
    redirect_in = {};
    checks++;
    restart(7, 1);
    check_repair_request();
    restart(8);
    check_repair_request();
    for (int test_case = 9; test_case <= 10; test_case++) {
      restart(test_case);
      instructions_in.pready = 0;
      inject_younger_error = test_case == 9;
      inject_younger_replay = test_case == 10;
      until([&] {
        return memory_in.presponse.pvalid &&
               (memory_in.presponse.pbits.paccess_ufault ||
                memory_in.presponse.pbits.preplay);
      });
      falling();
      instructions_in.pready = 1;
      check_repair_request();
    }
    restart(11, 1);
    until([&] { return accepted_cycle >= 0; });
    falling();
    CHECK(target_cycle == source_cycle + 1 && target_cycle < accepted_cycle &&
          !memory_out.pflush);
    checks++;
    restart(12);
    check_repair_request();
    restart(13, 1);
    check_repair_request();
    restart(14, 1);
    instructions_in.pready = 0;
    until([&] { return source_cycle >= 0; });
    falling();
    virtual_lookup_in.pready = 0;
    falling();
    // The S1 result must be retained after S1 drains, even if training changes
    // that entry while the successor is blocked. Its S2 metadata stays original.
    branch_update_in = {UINT64_C(1),
                        {branch_pc, UINT64_C(0x700), UINT64_C(1), UINT64_C(0),
                         UINT64_C(1), UINT64_C(0), UINT64_C(0), UINT64_C(0),
                         branch_pc + 4}};
    falling();
    branch_update_in = {};
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index) {
      settle();
      CHECK(virtual_lookup_out.pvalid &&
            virtual_lookup_out.pbits == target_pc && target_cycle < 0);
      falling();
    }
    virtual_lookup_in.pready = 1;
    instructions_in.pready = 1;
    until([&] { return accepted_cycle >= 0; });
    falling();
    CHECK(target_cycle == accepted_cycle);
    checks++;
    restart(15);
    until([&] { return accepted_cycle >= 0; });
    falling();
    CHECK(target_cycle == source_cycle + 1 && target_cycle < accepted_cycle);
    checks++;
  });
}