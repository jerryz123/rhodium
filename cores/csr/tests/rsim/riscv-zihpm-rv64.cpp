// Exercises the riscv-zihpm-rv64 component contract through rsim.
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

constexpr int XLEN = 64;
using word_t = std::conditional_t<XLEN == 32, std::uint32_t, std::uint64_t>;

void access_csr(std::uint16_t address, std::uint8_t operation, word_t source,
                std::uint8_t source_specifier, bool expect_trap = 0,
                word_t expected_old = 0, bool check_old = 1, bool immediate = 0,
                std::uint8_t rd = 1) {
  std::uint32_t instruction;
  instruction = (std::uint32_t(address) << 20) |
                (std::uint32_t(source_specifier) << 15) |
                (unsigned(immediate) << 14) | (unsigned(operation) << 12) |
                (unsigned(rd) << 7) | 0x73;
  eval_csr();
  commit_in = {};
  commit_in.pvalid = 1;
  commit_in.pbits.ppc = UINT64_C(0x100);
  commit_in.pbits.pinstruction = instruction;
  commit_in.pbits.prd = rd;
  commit_in.pbits.pcsr_uoperation = operation;
  commit_in.pbits.pcsr_uaddress = address;
  commit_in.pbits.pcsr_usource = source;
  eval_csr();
  CHECK(redirect_out.pvalid == expect_trap && !translation_flush);
  if (expect_trap) {
    CHECK(!writeback_valid && redirect_out.pbits == 0);
  } else {
    CHECK(writeback_valid && (!check_old || writeback_value == expected_old));
  }
  tick_csr();
  commit_in = {};
  if (expect_trap)
    CHECK(privilege == 3);
}

void illegal_access(std::uint16_t address, std::uint8_t operation = 2,
                    std::uint8_t source_specifier = 0, bool immediate = 0,
                    std::uint8_t rd = 1) {
  std::uint32_t instruction;
  instruction = (std::uint32_t(address) << 20) |
                (std::uint32_t(source_specifier) << 15) |
                (unsigned(immediate) << 14) | (unsigned(operation) << 12) |
                (unsigned(rd) << 7) | 0x73;
  access_csr(address, operation, 0, source_specifier, 1, 0, 1, immediate, rd);
  access_csr(UINT64_C(0x342), 2, 0, 0, 0, 2);
  access_csr(UINT64_C(0x343), 2, 0, 0, 0, word_t(instruction));
  access_csr(UINT64_C(0x341), 2, 0, 0, 0, UINT64_C(0x100));
}

void writable_zero(std::uint16_t address) {
  access_csr(address, 2, 0, 0);
  for (int operation = 1; operation <= 3; operation++) {
    access_csr(address, ((operation)&low_mask(2)), UINT64_MAX, 1);
    access_csr(address, 2, 0, 0);
    access_csr(address, ((operation)&low_mask(2)), word_t(31), 31, 0, 0, 1, 1);
    access_csr(address, 2, 0, 0);
  }
}

void readonly_zero(std::uint16_t address) {
  access_csr(address, 2, 0, 0);
  access_csr(address, 3, 0, 0);
  access_csr(address, 2, 0, 0, 0, 0, 1, 1);
  access_csr(address, 3, 0, 0, 0, 0, 1, 1);
  illegal_access(address, 1, 0);
  illegal_access(address, 1, 0, 1, 0);

  illegal_access(address, 2, 1);
  illegal_access(address, 3, 1);
  illegal_access(address, 2, 1, 1);
  illegal_access(address, 3, 1, 1);
  access_csr(address, 2, 0, 0);
}

void enter_mode(std::uint8_t mode) {
  access_csr(UINT64_C(0x300), 1, word_t(mode) << 11, 1, 0, 0, 0);
  access_csr(UINT64_C(0x341), 1, UINT64_C(0x200), 1, 0, 0, 0);
  eval_csr();
  commit_in = {};
  commit_in.pvalid = 1;
  commit_in.pbits.pinstruction = UINT64_C(807403635);
  commit_in.pbits.paction = 3;
  eval_csr();
  CHECK(redirect_out.pvalid && redirect_out.pbits == UINT64_C(0x200));
  tick_csr();
  commit_in = {};
  CHECK(privilege == mode);
}

int main() {
  return run_test([] {
    reset = 1;
    interrupts = {};
    time_counter = 0;
    interrupt_boundary = 0;
    fp_update_in = {};
    cbo_operation = 0;
    commit_in = {};
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_csr();
    reset = 0;
    CHECK(privilege == 3);

    for (int value = 0; value < 2; value++) {
      access_csr(UINT64_C(0x301), 1, value != 0 ? UINT64_MAX : 0, 1, 0,
                 XLEN == 32 ? word_t(UINT64_C(0x40141103))
                            : word_t(UINT64_C(0x8000000000141103)));
      access_csr(UINT64_C(0x301), 2, 0, 0, 0,
                 XLEN == 32 ? word_t(UINT64_C(0x40141103))
                            : word_t(UINT64_C(0x8000000000141103)));
    }

    access_csr(UINT64_C(0x300), 1, word_t(UINT64_C(0x1c0000)), 1, 0, 0, 0);
    CHECK((mstatus & word_t(UINT64_C(0x1c0000))) == word_t(UINT64_C(0x180000)));
    access_csr(UINT64_C(0x100), 1, word_t(UINT64_C(0xc0000)), 1, 0, 0, 0);
    access_csr(UINT64_C(0x100), 2, 0, 0, 0,
               (XLEN == 32 ? word_t(0) : word_t(UINT64_C(0x200000000))) |
                   word_t(UINT64_C(0x80000)));
    readonly_zero(UINT64_C(0xf15));
    if (XLEN == 32) {
      writable_zero(UINT64_C(0x310));
      access_csr(UINT64_C(0x302), 1, 4, 1);
      writable_zero(UINT64_C(0x312));
      access_csr(UINT64_C(0x302), 2, 0, 0, 0, 4);
      access_csr(UINT64_C(0x302), 1, 0, 1, 0, 4);
      writable_zero(UINT64_C(0x31a));
    } else {
      illegal_access(UINT64_C(0x310));
      illegal_access(UINT64_C(0x312));
      illegal_access(UINT64_C(0x31a));
    }
    for (int index = 3; index <= 31; index++) {
      writable_zero(((UINT64_C(0xb00) + index) & low_mask(12)));
      writable_zero(((UINT64_C(0x320) + index) & low_mask(12)));
      readonly_zero(((UINT64_C(0xc00) + index) & low_mask(12)));
      if (XLEN == 32) {
        writable_zero(((UINT64_C(0xb80) + index) & low_mask(12)));
        readonly_zero(((UINT64_C(0xc80) + index) & low_mask(12)));
      } else {
        illegal_access(((UINT64_C(0xb80) + index) & low_mask(12)));
        illegal_access(((UINT64_C(0xc80) + index) & low_mask(12)));
      }
    }

    access_csr(UINT64_C(0x306), 1, UINT64_MAX, 1);
    access_csr(UINT64_C(0x106), 1, UINT64_MAX, 1);
    access_csr(UINT64_C(0x306), 2, 0, 0, 0, 7);
    access_csr(UINT64_C(0x106), 2, 0, 0, 0, 7);
    for (int mode = 0; mode <= 1; mode++) {
      enter_mode(((mode)&low_mask(2)));
      illegal_access(UINT64_C(0xf15));
      if (XLEN == 32) {
        enter_mode(((mode)&low_mask(2)));
        illegal_access(UINT64_C(0x310));
        enter_mode(((mode)&low_mask(2)));
        illegal_access(UINT64_C(0x312));
        enter_mode(((mode)&low_mask(2)));
        illegal_access(UINT64_C(0x31a));
      }
      for (int index = 3; index <= 31; index++) {
        enter_mode(((mode)&low_mask(2)));
        illegal_access(((UINT64_C(0xc00) + index) & low_mask(12)));
        if (XLEN == 32) {
          enter_mode(((mode)&low_mask(2)));
          illegal_access(((UINT64_C(0xc80) + index) & low_mask(12)));
        }
        enter_mode(((mode)&low_mask(2)));
        illegal_access(((UINT64_C(0xb00) + index) & low_mask(12)));
        enter_mode(((mode)&low_mask(2)));
        illegal_access(((UINT64_C(0x320) + index) & low_mask(12)));
      }
    }
    for (unsigned repeat_index = 0; repeat_index < (10); ++repeat_index)
      eval_csr();
    for (int index = 3; index <= 31; index++)
      access_csr(((UINT64_C(0xc00) + index) & low_mask(12)), 2, 0, 0);
  });
}
