// Exercises the riscv-sscofpmf-rv64 component contract through rsim.
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
bool virtualized_value() { return false; }

constexpr int XLEN = 64;
using word_t = std::conditional_t<XLEN == 32, std::uint32_t, std::uint64_t>;
constexpr bool HYPERVISOR = 0;

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

void wr(std::uint16_t address, word_t value) {
  access_csr(address, 1, value, 1, 0, 0, 0);
}
void rd(std::uint16_t address, word_t value) {
  access_csr(address, 2, 0, 0, 0, value);
}
void wr64(std::uint16_t address, std::uint64_t value) {
  wr(address, word_t(value));
  if (XLEN == 32)
    wr(address == UINT64_C(0x323) ? UINT64_C(1827) : UINT64_C(2947),
       word_t(value >> 32));
}
void freeze() { wr(UINT64_C(0x320), 8); }
void reset_state() {
  eval_csr();
  commit_in = {};
  interrupt_boundary = 0;
  interrupts = {};
  reset = 1;
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
    tick_csr();
  eval_csr();
  reset = 0;
}
void retire_nop() {
  eval_csr();
  commit_in = {};
  commit_in.pvalid = 1;
  commit_in.pbits.pinstruction = UINT64_C(0x13);
  tick_csr();
  commit_in = {};
}
void enter_guest(std::uint8_t mode) {
  wr(UINT64_C(0x300), (word_t(1) << 39) | (word_t(mode) << 11));
  wr(UINT64_C(0x341), UINT64_C(0x200));
  eval_csr();
  commit_in = {};
  commit_in.pvalid = 1;
  commit_in.pbits.paction = 3;
  commit_in.pbits.pinstruction = UINT64_C(0x30200073);
  tick_csr();
  commit_in = {};
  CHECK(virtualized_value() && privilege == mode);
}

int main() {
  return run_test([] {
    reset = 1;
    interrupts = {};
    time_counter = 0;
    interrupt_boundary = 0;
    fp_update_in = {};
    cbo_operation = 0;
    reset_state();
    freeze();
    rd(UINT64_C(0x320), 8);
    wr(UINT64_C(0x320), UINT64_MAX);
    rd(UINT64_C(0x320), 8);
    wr64(UINT64_C(0xb03), UINT64_C(1311768467463790320));
    rd(UINT64_C(0xb03), word_t(UINT64_C(1311768467463790320)));
    rd(UINT64_C(0xc03), word_t(UINT64_C(1311768467463790320)));
    if (XLEN == 32) {
      rd(UINT64_C(0xb83), UINT64_C(0x12345678));
      rd(UINT64_C(0xc83), UINT64_C(0x12345678));
    }

    for (int i = 4; i < 32; ++i) {
      wr(UINT64_C(2816) + ((i)&low_mask(12)), UINT64_MAX);
      rd(UINT64_C(2816) + ((i)&low_mask(12)), 0);
      wr(UINT64_C(800) + ((i)&low_mask(12)), UINT64_MAX);
      rd(UINT64_C(800) + ((i)&low_mask(12)), 0);
      if (XLEN == 32) {
        wr(UINT64_C(1824) + ((i)&low_mask(12)), UINT64_MAX);
        rd(UINT64_C(1824) + ((i)&low_mask(12)), 0);
      }
    }
    wr64(UINT64_C(0x323), UINT64_C(18158513697557839872));
    rd(UINT64_C(0x323),
       XLEN == 64 ? word_t(HYPERVISOR ? UINT64_C(18158513697557839872)
                                      : UINT64_C(17293822569102704640))
                  : 0);
    if (XLEN == 32)
      rd(UINT64_C(0x723), word_t(UINT64_C(0xf0000000)));
    else
      illegal_access(UINT64_C(0x723));
    rd(UINT64_C(0xda0), 8);
    wr64(UINT64_C(0x323), UINT64_C(18446744073709551615));
    rd(UINT64_C(0x323),
       XLEN == 64 ? word_t(HYPERVISOR ? UINT64_C(18158513697557839872)
                                      : UINT64_C(17293822569102704640))
                  : 0);

    rd(UINT64_C(0x344), 0);
    illegal_access(UINT64_C(0xda0), 1, 1);

    for (int index = 0; index < 32; ++index) {
      wr(UINT64_C(0x106), word_t(1) << index);
      rd(UINT64_C(0x106), index < 4 ? word_t(1) << index : 0);
      wr(UINT64_C(0x106), 0);
      rd(UINT64_C(0x106), 0);
      if (HYPERVISOR) {
        wr(UINT64_C(0x606), word_t(1) << index);
        rd(UINT64_C(0x606), index < 4 ? word_t(1) << index : 0);
        wr(UINT64_C(0x606), 0);
        rd(UINT64_C(0x606), 0);
      }
    }
    wr(UINT64_C(0x306), UINT64_MAX);
    rd(UINT64_C(0x306), 15);
    wr(UINT64_C(0x106), UINT64_MAX);
    rd(UINT64_C(0x106), 15);
    enter_mode(1);
    wr(UINT64_C(0x106), 0);
    rd(UINT64_C(0x106), 0);
    wr(UINT64_C(0x106), 8);
    rd(UINT64_C(0x106), 8);
    rd(UINT64_C(0xda0), 8);
    rd(UINT64_C(0xc03), word_t(UINT64_C(1311768467463790320)));
    illegal_access(UINT64_C(0xc04));
    wr(UINT64_C(0x306), 0);
    enter_mode(1);
    rd(UINT64_C(0xda0), 0);
    illegal_access(UINT64_C(0xc03));
    wr(UINT64_C(0x306), 8);
    wr(UINT64_C(0x106), 0);
    enter_mode(0);
    illegal_access(UINT64_C(0xc03));
    wr(UINT64_C(0x106), 8);
    enter_mode(0);
    rd(UINT64_C(0xc03), word_t(UINT64_C(1311768467463790320)));
    illegal_access(UINT64_C(0xda0));

    reset_state();
    wr64(UINT64_C(0x323), 2);
    wr64(UINT64_C(0xb03), 0);
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index)
      tick_csr();
    retire_nop();
    retire_nop();
    eval_csr();
    commit_in = {};
    commit_in.pvalid = 1;
    commit_in.pbits.pexception_uvalid = 1;
    commit_in.pbits.pexception_ucause = 2;
    tick_csr();
    commit_in = {};
    freeze();
    rd(UINT64_C(0xb03), 3);

    wr64(UINT64_C(0x323), UINT64_C(4611686018427387906));
    wr64(UINT64_C(0xb03), 0);
    wr(UINT64_C(0x320), 0);
    retire_nop();
    freeze();
    rd(UINT64_C(0xb03), 0);

    for (int mode = 0; mode < (HYPERVISOR ? 4 : 2); ++mode) {
      for (int filtered = 0; filtered < 2; ++filtered) {
        reset_state();
        wr64(UINT64_C(0x323),
             UINT64_C(4611686018427387906) |
                 (filtered != 0 ? (UINT64_C(1) << (mode == 0   ? 60
                                                   : mode == 1 ? 61
                                                   : mode == 2 ? 59
                                                               : 58))
                                : 0));
        wr64(UINT64_C(0xb03), 0);
        if (mode < 2)
          enter_mode(((mode)&low_mask(2)));
        else
          enter_guest(mode == 2 ? UINT64_C(1) : UINT64_C(0));
        retire_nop();
        retire_nop();
        eval_csr();
        commit_in = {};
        commit_in.pvalid = 1;
        commit_in.pbits.pexception_uvalid = 1;
        commit_in.pbits.pexception_ucause = 2;
        tick_csr();
        commit_in = {};
        freeze();
        rd(UINT64_C(0xb03), filtered != 0 ? 0 : 2);
      }
    }

    reset_state();
    wr64(UINT64_C(0x323), 1);
    wr64(UINT64_C(0xb03), 0);
    for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
      tick_csr();
    freeze();
    eval_csr();
    commit_in = {};
    commit_in.pvalid = 1;
    commit_in.pbits.pcsr_uoperation = 2;
    commit_in.pbits.pcsr_uaddress = UINT64_C(0xb03);
    commit_in.pbits.prd = 1;
    eval_csr();
    CHECK(writeback_valid && writeback_value >= 5);
    tick_csr();
    commit_in = {};

    reset_state();
    wr64(UINT64_C(0x323), 2);
    wr64(UINT64_C(0xb03), UINT64_MAX);
    wr(UINT64_C(0x344), 0);
    freeze();
    rd(UINT64_C(0xda0), 8);
    rd(UINT64_C(0x344), UINT64_C(0x2000));
    wr(UINT64_C(0x344), 0);
    rd(UINT64_C(0x344), 0);
    rd(UINT64_C(0xda0), 8);
    wr64(UINT64_C(0xb03), UINT64_MAX);
    wr(UINT64_C(0x320), 0);
    retire_nop();
    freeze();
    rd(UINT64_C(0x344), 0);
    wr64(UINT64_C(0x323), 2);
    wr64(UINT64_C(0xb03), UINT64_MAX);
    wr(UINT64_C(0x320), 0);
    retire_nop();
    freeze();
    rd(UINT64_C(0x344), UINT64_C(0x2000));

    wr(UINT64_C(0x304), UINT64_C(0x2000));
    CHECK(wfi_wake && !interrupt_request);
    wr(UINT64_C(0x300), 8);
    CHECK(interrupt_request);
    eval_csr();
    interrupt_pc = UINT64_C(0x444);
    interrupt_boundary = 1;
    eval_csr();
    CHECK(redirect_out.pvalid);
    tick_csr();
    interrupt_boundary = 0;
    rd(UINT64_C(0x342), (word_t(1) << (XLEN - 1)) | 13);
    rd(UINT64_C(0x341), UINT64_C(0x444));
    rd(UINT64_C(0x344), UINT64_C(0x2000));
    wr(UINT64_C(0x344), 0);

    wr(UINT64_C(0x303), UINT64_C(0x2000));
    wr(UINT64_C(0x344), UINT64_C(0x2000));
    enter_mode(0);
    eval_csr();
    interrupt_pc = UINT64_C(0x448);
    interrupt_boundary = 1;
    tick_csr();
    interrupt_boundary = 0;
    CHECK(privilege == 1);
    rd(UINT64_C(0x142), (word_t(1) << (XLEN - 1)) | 13);
    rd(UINT64_C(0x141), UINT64_C(0x448));
    rd(UINT64_C(0x144), UINT64_C(0x2000));
    wr(UINT64_C(0x144), 0);
    rd(UINT64_C(0x144), 0);

    if (HYPERVISOR) {
      reset_state();
      freeze();
      wr64(UINT64_C(0x323), UINT64_C(9223372036854775808));
      wr(UINT64_C(0x306), 8);
      wr(UINT64_C(0x606), 0);
      enter_guest(1);
      rd(UINT64_C(0xda0), 0);
      access_csr(UINT64_C(0xc03), 2, 0, 0, 1);
      rd(UINT64_C(0x342), 22);
      wr(UINT64_C(0x606), 8);
      enter_guest(1);
      rd(UINT64_C(0xda0), 8);
      rd(UINT64_C(0xc03), 0);

      illegal_access(UINT64_C(0x300));
      wr(UINT64_C(0x303), UINT64_C(0x2000));
      wr(UINT64_C(0x603), UINT64_C(0x2000));
      rd(UINT64_C(0x603), 0);
      wr(UINT64_C(0x304), UINT64_C(0x2000));
      wr(UINT64_C(0x344), UINT64_C(0x2000));
      enter_guest(1);
      eval_csr();
      interrupt_pc = UINT64_C(0x450);
      interrupt_boundary = 1;
      tick_csr();
      interrupt_boundary = 0;
      CHECK(!virtualized_value() && privilege == 1);
      rd(UINT64_C(0x142), (word_t(1) << 63) | 13);

      reset_state();
      wr(UINT64_C(0x303), UINT64_C(0x2000));
      wr(UINT64_C(0x304), UINT64_C(0x2040));
      wr(UINT64_C(0x344), UINT64_C(0x2000));
      wr(UINT64_C(0x645), UINT64_C(0x40));
      enter_guest(1);
      eval_csr();
      interrupt_pc = UINT64_C(0x454);
      interrupt_boundary = 1;
      tick_csr();
      interrupt_boundary = 0;
      CHECK(!virtualized_value() && privilege == 1);
      rd(UINT64_C(0x142), (word_t(1) << 63) | 6);

      reset_state();
      wr(UINT64_C(0x303), UINT64_C(0x2000));
      wr(UINT64_C(0x603), UINT64_C(0x40));
      wr(UINT64_C(0x304), UINT64_C(0x2040));
      wr(UINT64_C(0x200), 2);
      wr(UINT64_C(0x344), UINT64_C(0x2000));
      wr(UINT64_C(0x645), UINT64_C(0x40));
      enter_guest(1);
      eval_csr();
      interrupt_pc = UINT64_C(0x458);
      interrupt_boundary = 1;
      tick_csr();
      interrupt_boundary = 0;
      CHECK(!virtualized_value() && privilege == 1);
      rd(UINT64_C(0x142), (word_t(1) << 63) | 13);
    }
    reset_state();
    count_commands = 0;
    wr64(UINT64_C(0x323), 2);
    wr64(UINT64_C(0xb03), UINT64_C(18446744073709551614));
    eval_csr();
    independent_retire = 2;
    tick_csr();
    eval_csr();
    independent_retire = 0;
    rd(UINT64_C(0xb03), 0);
    rd(UINT64_C(0xda0), 8);
    rd(UINT64_C(0x344), UINT64_C(0x2000));
  });
}
