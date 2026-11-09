// Exercises the riscv-sstc-rv32 component contract through rsim.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
bool count_commands = true;
unsigned independent_retire = 0;
void eval_csr() {
  eval();
  retire_count = count_commands ? command_success : independent_retire;
  eval();
}
void tick_csr() {
  eval_csr();
  tick_model();
  eval_csr();
}

void csr(bool write, std::uint16_t address, std::uint32_t value,
         std::uint32_t expected = 0) {
  eval_csr();
  commit_in = {};
  commit_in.pvalid = 1;
  commit_in.pbits.prd = 1;
  commit_in.pbits.pcsr_uoperation = write ? 1 : 2;
  commit_in.pbits.pcsr_uaddress = address;
  commit_in.pbits.pcsr_usource = value;
  eval_csr();
  CHECK(command_success && writeback_valid && !redirect_out.pvalid);
  if (!write)
    CHECK(writeback_value == expected);
  tick_csr();
  commit_in = {};
}

void enter_supervisor() {
  csr(1, UINT64_C(0x300), UINT64_C(0x800));
  csr(1, UINT64_C(0x341), UINT64_C(0x800));
  eval_csr();
  commit_in = {};
  commit_in.pvalid = 1;
  commit_in.pbits.paction = 3;
  tick_csr();
  commit_in = {};
  CHECK(privilege == 1);
}

void denied(std::uint16_t address) {
  eval_csr();
  commit_in = {};
  commit_in.pvalid = 1;
  commit_in.pbits.ppc = UINT64_C(0x880);
  commit_in.pbits.prd = 1;
  commit_in.pbits.pcsr_uoperation = 1;
  commit_in.pbits.pcsr_uaddress = address;
  commit_in.pbits.pcsr_usource = UINT64_MAX;
  eval_csr();
  CHECK(!command_success && !writeback_valid && redirect_out.pvalid &&
        redirect_out.pbits == UINT64_C(0x1000));
  tick_csr();
  commit_in = {};
  csr(0, UINT64_C(0x342), 0, 2);
  csr(0, UINT64_C(0x341), 0, UINT64_C(0x880));
}

int main() {
  return run_test([] {
    reset = 1;
    interrupts = {};
    time_counter = UINT64_C(1311768469162688511);
    hart_id = 0;
    interrupt_pc = UINT64_C(0x888);
    interrupt_boundary = 0;
    commit_in = {};
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_csr();
    reset = 0;
    csr(1, UINT64_C(0x305), UINT64_C(0x1000));
    csr(1, UINT64_C(0x105), UINT64_C(0x2000));
    for (int i = 0; i < 4; i++) {
      csr(0, ((UINT64_C(0x31c) + i) & low_mask(12)), 0, 0);
      csr(1, ((UINT64_C(0x30c) + i) & low_mask(12)), UINT64_MAX);
      csr(0, ((UINT64_C(0x30c) + i) & low_mask(12)), 0, 0);
      csr(1, ((UINT64_C(0x31c) + i) & low_mask(12)), UINT64_MAX);
      csr(0, ((UINT64_C(0x31c) + i) & low_mask(12)), 0,
          i == 0 ? UINT64_C(0xc0000000) : UINT64_C(0x80000000));
      csr(1, ((UINT64_C(0x30c) + i) & low_mask(12)), 0);
      csr(0, ((UINT64_C(0x31c) + i) & low_mask(12)), 0,
          i == 0 ? UINT64_C(0xc0000000) : UINT64_C(0x80000000));
      csr(1, ((UINT64_C(0x10c) + i) & low_mask(12)), UINT64_MAX);
      csr(0, ((UINT64_C(0x10c) + i) & low_mask(12)), 0, 0);
      csr(1, ((UINT64_C(0x31c) + i) & low_mask(12)), 0);
      enter_supervisor();
      denied(((UINT64_C(0x10c) + i) & low_mask(12)));
      csr(1, ((UINT64_C(0x31c) + i) & low_mask(12)), UINT64_C(0x80000000));
      enter_supervisor();
      csr(1, ((UINT64_C(0x10c) + i) & low_mask(12)), UINT64_MAX);
      csr(0, ((UINT64_C(0x10c) + i) & low_mask(12)), 0, 0);
      denied(((UINT64_C(0x31c) + i) & low_mask(12)));
    }
    enter_supervisor();
    denied(UINT64_C(0x10a));
    csr(1, UINT64_C(0x31c), UINT64_C(0xc0000000));
    enter_supervisor();
    csr(1, UINT64_C(0x10a), 1);
    csr(0, UINT64_C(0x10a), 0, 1);
    denied(UINT64_C(0x30c));
    csr(0, UINT64_C(0x10a), 0, 1);
    csr(1, UINT64_C(0x14d), UINT64_C(0xffffffff));
    csr(1, UINT64_C(0x15d), UINT64_C(0x12345679));
    csr(0, UINT64_C(0x14d), 0, UINT64_C(0xffffffff));
    csr(0, UINT64_C(0x15d), 0, UINT64_C(0x12345679));
    csr(1, UINT64_C(0x31a), UINT64_C(0x80000000));
    csr(0, UINT64_C(0x31a), 0, UINT64_C(0x80000000));
    csr(1, UINT64_C(0x30a), 1);
    csr(0, UINT64_C(0x30a), 0, 1);
    csr(0, UINT64_C(0x31a), 0, UINT64_C(0x80000000));
    csr(0, UINT64_C(0x344), 0, 0);
    csr(1, UINT64_C(0x14d), 0);
    csr(0, UINT64_C(0x15d), 0, UINT64_C(0x12345679));
    csr(0, UINT64_C(0x344), 0, 0);
    time_counter = UINT64_C(1311768469162688512);
    csr(0, UINT64_C(0x344), 0, UINT64_C(0x20));
    csr(1, UINT64_C(0x15d), UINT64_C(0xffffffff));
    csr(0, UINT64_C(0x14d), 0, 0);
    csr(0, UINT64_C(0x344), 0, 0);

    time_counter = UINT64_C(9223372036854775808);
    csr(0, UINT64_C(0x344), 0, 0);
    csr(1, UINT64_C(0x15d), UINT64_C(0x80000000));
    csr(0, UINT64_C(0x344), 0, UINT64_C(0x20));
    csr(1, UINT64_C(0x14d), 1);
    csr(0, UINT64_C(0x344), 0, 0);

    csr(1, UINT64_C(0x344), UINT64_C(0x20));
    csr(0, UINT64_C(0x344), 0, 0);
    csr(1, UINT64_C(0x31a), 0);
    csr(1, UINT64_C(0x344), UINT64_C(0x20));
    csr(0, UINT64_C(0x344), 0, UINT64_C(0x20));
    csr(1, UINT64_C(0x344), 0);
    csr(1, UINT64_C(0x31a), UINT64_C(0x80000000));
    csr(1, UINT64_C(0x306), 2);
    csr(1, UINT64_C(0x303), UINT64_C(0x20));
    csr(1, UINT64_C(0x304), UINT64_C(0x20));
    csr(1, UINT64_C(0x300), UINT64_C(0x802));
    csr(1, UINT64_C(0x341), UINT64_C(0x800));
    eval_csr();
    commit_in = {};
    commit_in.pvalid = 1;
    commit_in.pbits.paction = 3;
    tick_csr();
    commit_in = {};
    CHECK(privilege == 1);
    csr(0, UINT64_C(0x15d), 0, UINT64_C(0x80000000));
    csr(1, UINT64_C(0x14d), 2);
    time_counter = UINT64_C(9223372036854775810);
    eval_csr();
    CHECK(interrupt_request && wfi_wake);
    eval_csr();
    interrupt_boundary = 1;
    eval_csr();
    CHECK(redirect_out.pvalid && redirect_out.pbits == UINT64_C(0x2000));
    tick_csr();
    interrupt_boundary = 0;
    csr(0, UINT64_C(0x142), 0, UINT64_C(0x80000005));
    csr(0, UINT64_C(0x141), 0, UINT64_C(0x888));
    csr(1, UINT64_C(0x14d), 3);
    csr(0, UINT64_C(0x144), 0, 0);
  });
}
