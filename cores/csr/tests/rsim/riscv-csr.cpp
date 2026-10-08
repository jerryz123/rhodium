// Exercises the riscv-csr component contract through rsim.
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

constexpr std::uint8_t CSR_NONE = UINT64_C(0);
constexpr std::uint8_t CSR_WRITE = UINT64_C(1);
constexpr std::uint8_t CSR_SET = UINT64_C(2);
constexpr std::uint8_t CSR_CLEAR = UINT64_C(3);
constexpr std::uint16_t CSR_FFLAGS = UINT64_C(1);
constexpr std::uint16_t CSR_FRM = UINT64_C(2);
constexpr std::uint16_t CSR_FCSR = UINT64_C(3);
constexpr std::uint16_t CSR_SENVCFG = UINT64_C(266);
constexpr std::uint16_t CSR_STVEC = UINT64_C(261);
constexpr std::uint16_t CSR_SSTATUS = UINT64_C(256);
constexpr std::uint16_t CSR_SCOUNTEREN = UINT64_C(262);
constexpr std::uint16_t CSR_SEPC = UINT64_C(321);
constexpr std::uint16_t CSR_SCAUSE = UINT64_C(322);
constexpr std::uint16_t CSR_SIP = UINT64_C(324);
constexpr std::uint16_t CSR_SATP = UINT64_C(384);
constexpr std::uint16_t CSR_MSTATUS = UINT64_C(768);
constexpr std::uint16_t CSR_MISA = UINT64_C(769);
constexpr std::uint16_t CSR_MEDELEG = UINT64_C(770);
constexpr std::uint16_t CSR_MIDELEG = UINT64_C(771);
constexpr std::uint16_t CSR_MIE = UINT64_C(772);
constexpr std::uint16_t CSR_MTVEC = UINT64_C(773);
constexpr std::uint16_t CSR_MCOUNTEREN = UINT64_C(774);
constexpr std::uint16_t CSR_MENVCFG = UINT64_C(778);
constexpr std::uint16_t CSR_MSCRATCH = UINT64_C(832);
constexpr std::uint16_t CSR_MEPC = UINT64_C(833);
constexpr std::uint16_t CSR_MCAUSE = UINT64_C(834);
constexpr std::uint16_t CSR_MTVAL = UINT64_C(835);
constexpr std::uint16_t CSR_MIP = UINT64_C(836);
constexpr std::uint16_t CSR_MCYCLE = UINT64_C(2816);
constexpr std::uint16_t CSR_MINSTRET = UINT64_C(2818);
constexpr std::uint16_t CSR_CYCLE = UINT64_C(3072);
constexpr std::uint16_t CSR_TIME = UINT64_C(3073);
constexpr std::uint16_t CSR_INSTRET = UINT64_C(3074);
constexpr std::uint16_t CSR_MHARTID = UINT64_C(3860);
constexpr std::uint8_t SYSTEM_NONE = UINT64_C(0);
constexpr std::uint8_t SYSTEM_ECALL = UINT64_C(1);
constexpr std::uint8_t SYSTEM_EBREAK = UINT64_C(2);
constexpr std::uint8_t SYSTEM_MRET = UINT64_C(3);
constexpr std::uint8_t SYSTEM_SRET = UINT64_C(4);
constexpr std::uint8_t SYSTEM_WFI = UINT64_C(5);
constexpr std::uint8_t FENCE_NONE = UINT64_C(0);
constexpr std::uint8_t FENCE_ADDRESS_TRANSLATION = UINT64_C(3);
constexpr std::uint8_t PRIVILEGE_U = UINT64_C(0);
constexpr std::uint8_t PRIVILEGE_S = UINT64_C(1);
constexpr std::uint8_t PRIVILEGE_M = UINT64_C(3);
constexpr std::uint64_t RV64_MSTATUS_FIXED = UINT64_C(42949672960);
constexpr std::uint64_t RV64_SSTATUS_FIXED = UINT64_C(8589934592);
constexpr std::uint64_t RV64_MISA_DC = UINT64_C(9223372036856090927);
constexpr std::uint64_t MSTATUS_FS_INITIAL = UINT64_C(8192);
constexpr std::uint64_t MSTATUS_FS_DIRTY = UINT64_C(24576);
constexpr std::uint64_t MSTATUS_SD = UINT64_C(9223372036854775808);
constexpr std::uint64_t MSTATUS_MPP_S = UINT64_C(2048);
constexpr std::uint64_t MSTATUS_TVM = UINT64_C(1048576);
constexpr std::uint64_t MSTATUS_TW = UINT64_C(2097152);
constexpr std::uint64_t MSTATUS_TSR = UINT64_C(4194304);

void clear_commit() {
  commit_in = {};
  fp_update_in = {};
  interrupt_boundary = UINT64_C(0);
}

void reset_dut() {
  reset = UINT64_C(1);
  interrupts = {};
  interrupt_pc = {};
  clear_commit();
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
    tick_csr();
  reset = UINT64_C(0);
  CHECK(privilege == PRIVILEGE_M && satp == 0 &&
        mstatus == RV64_MSTATUS_FIXED && !fp_enabled && frm == 0);
  CHECK(!command_success && !wfi && !wfi_wake);
}

void fp_state_update(std::uint8_t flags) {
  eval_csr();
  clear_commit();
  fp_update_in.pvalid = UINT64_C(1);
  fp_update_in.pbits = flags;
  tick_csr();
  clear_commit();
}

void take_interrupt(std::uint64_t pc, std::uint64_t expected_target,
                    std::uint8_t expected_privilege) {
  eval_csr();
  clear_commit();
  interrupt_pc = pc;
  interrupt_boundary = UINT64_C(1);
  eval_csr();
  CHECK(interrupt_request && redirect_out.pvalid &&
        redirect_out.pbits == expected_target);
  tick_csr();
  clear_commit();
  interrupts = {};
  CHECK(privilege == expected_privilege);
}

void csr_access(std::uint8_t operation, std::uint16_t address,
                std::uint64_t source, std::uint64_t expected_old,
                bool expect_flush = UINT64_C(0)) {
  eval_csr();
  clear_commit();
  commit_in.pvalid = UINT64_C(1);
  commit_in.pbits.prd = UINT64_C(1);
  commit_in.pbits.pcsr_uoperation = operation;
  commit_in.pbits.pcsr_uaddress = address;
  commit_in.pbits.pcsr_usource = source;
  if ((operation == CSR_SET || operation == CSR_CLEAR) && source != 0)
    commit_in.pbits.pinstruction =
        (commit_in.pbits.pinstruction & ~(low_mask(5) << 15)) |
        ((UINT64_C(1)) << 15);
  eval_csr();
  CHECK(command_success && writeback_valid && writeback_value == expected_old);
  CHECK(!redirect_out.pvalid && translation_flush == expect_flush);
  if (address == UINT64_C(266)) {
    std::uint64_t replacement;
    std::uint8_t old_mode, new_mode;
    replacement = operation == CSR_WRITE ? source
                  : operation == CSR_SET ? expected_old | source
                                         : expected_old & ~source;
    old_mode =
        slice(expected_old, 33, 32) == 1 ? 0 : slice(expected_old, 33, 32);
    new_mode = slice(replacement, 33, 32) == 1 ? 0 : slice(replacement, 33, 32);
    CHECK(pointer_masking_changed == (old_mode != new_mode));
  }
  tick_csr();
  clear_commit();
}

void csr_read_legal(std::uint16_t address) {
  eval_csr();
  clear_commit();
  commit_in.pvalid = UINT64_C(1);
  commit_in.pbits.prd = UINT64_C(1);
  commit_in.pbits.pcsr_uoperation = CSR_SET;
  commit_in.pbits.pcsr_uaddress = address;
  eval_csr();
  CHECK(command_success && writeback_valid && !redirect_out.pvalid &&
        !translation_flush);
  tick_csr();
  clear_commit();
}

void csr_write_intent_traps(std::uint8_t operation, std::uint16_t address,
                            std::uint8_t source_specifier) {
  eval_csr();
  clear_commit();
  commit_in.pvalid = UINT64_C(1);
  commit_in.pbits.prd = UINT64_C(1);
  commit_in.pbits.pcsr_uoperation = operation;
  commit_in.pbits.pcsr_uaddress = address;
  commit_in.pbits.pinstruction =
      (commit_in.pbits.pinstruction & ~(low_mask(5) << 15)) |
      ((source_specifier) << 15);
  eval_csr();
  CHECK(!command_success && !writeback_valid && redirect_out.pvalid);
  CHECK(!pointer_masking_changed);
  tick_csr();
  clear_commit();
}

void system_action(std::uint8_t operation, std::uint64_t pc,
                   std::uint64_t expected_target) {
  eval_csr();
  clear_commit();
  commit_in.pvalid = UINT64_C(1);
  commit_in.pbits.ppc = pc;
  switch (operation) {
  case SYSTEM_EBREAK:
    commit_in.pbits.pinstruction = UINT64_C(1048691);

    break;
  case SYSTEM_MRET:
    commit_in.pbits.pinstruction = UINT64_C(807403635);

    break;
  case SYSTEM_SRET:
    commit_in.pbits.pinstruction = UINT64_C(270532723);

    break;
  case SYSTEM_WFI:
    commit_in.pbits.pinstruction = UINT64_C(273678451);

    break;
  default:
    commit_in.pbits.pinstruction = UINT64_C(115);

    break;
  }
  commit_in.pbits.paction = operation;
  eval_csr();
  CHECK(redirect_out.pvalid && redirect_out.pbits == expected_target);
  tick_csr();
  clear_commit();
}

void explicit_exception(std::uint64_t cause, std::uint64_t pc,
                        std::uint64_t expected_target,
                        std::uint8_t expected_privilege) {
  eval_csr();
  clear_commit();
  commit_in.pvalid = UINT64_C(1);
  commit_in.pbits.ppc = pc;
  commit_in.pbits.pexception_uvalid = UINT64_C(1);
  commit_in.pbits.pexception_ucause = cause;
  eval_csr();
  CHECK(redirect_out.pvalid && redirect_out.pbits == expected_target);
  tick_csr();
  clear_commit();
  CHECK(privilege == expected_privilege);
}

void privileged_action(std::uint8_t system_operation,
                       std::uint8_t fence_operation, std::uint64_t pc,
                       std::uint32_t instruction, bool expect_trap,
                       bool expect_flush) {
  eval_csr();
  clear_commit();
  commit_in.pvalid = UINT64_C(1);
  commit_in.pbits.ppc = pc;
  commit_in.pbits.pinstruction = instruction;
  commit_in.pbits.paction =
      fence_operation == FENCE_NONE ? system_operation : UINT64_C(7);
  eval_csr();
  CHECK(redirect_out.pvalid == expect_trap);
  CHECK(command_success == !expect_trap);
  if (expect_trap)
    CHECK(redirect_out.pbits == UINT64_C(256));
  if (system_operation == SYSTEM_WFI)
    CHECK(wfi == !expect_trap);
  else
    CHECK(!wfi);
  CHECK(translation_flush == expect_flush);
  tick_csr();
  clear_commit();
}

void illegal_csr_read(std::uint16_t address, std::uint64_t pc,
                      std::uint32_t instruction) {
  eval_csr();
  clear_commit();
  commit_in.pvalid = UINT64_C(1);
  commit_in.pbits.ppc = pc;
  commit_in.pbits.pinstruction = instruction;
  commit_in.pbits.prd = UINT64_C(1);
  commit_in.pbits.pcsr_uoperation = CSR_SET;
  commit_in.pbits.pcsr_uaddress = address;
  eval_csr();
  CHECK(!writeback_valid && redirect_out.pvalid &&
        redirect_out.pbits == UINT64_C(256));
  CHECK(!translation_flush);
  tick_csr();
  clear_commit();
}

void enter_supervisor(std::uint64_t mstatus_flags) {
  csr_access(CSR_WRITE, CSR_MTVEC, UINT64_C(256), UINT64_C(0));
  csr_access(CSR_WRITE, CSR_MSTATUS, mstatus_flags | MSTATUS_MPP_S,
             RV64_MSTATUS_FIXED);
  csr_access(CSR_WRITE, CSR_MEPC, UINT64_C(512), UINT64_C(0));
  system_action(SYSTEM_MRET, UINT64_C(0), UINT64_C(512));
  CHECK(privilege == PRIVILEGE_S);
}

void check_illegal_trap(std::uint64_t pc, std::uint32_t instruction) {
  CHECK(privilege == PRIVILEGE_M);
  csr_access(CSR_SET, CSR_MCAUSE, UINT64_C(0), UINT64_C(2));
  csr_access(CSR_SET, CSR_MEPC, UINT64_C(0), pc);
  csr_access(CSR_SET, CSR_MTVAL, UINT64_C(0), instruction);
}

int main() {
  return run_test([] {
    cbo_operation = 0;
    reset = UINT64_C(1);
    time_counter = UINT64_C(1311768467463790320);
    hart_id = UINT64_C(7);
    reset_dut();

    for (int index = 2; index < 64; index++) {
      std::uint64_t target = UINT64_C(1) << index;
      csr_access(CSR_WRITE, CSR_MEDELEG, UINT64_C(1) << 9, 0);
      enter_supervisor(0);
      csr_access(CSR_WRITE, CSR_STVEC, target, 0);
      csr_access(CSR_SET, CSR_STVEC, 0, target);
      csr_access(CSR_SET, CSR_SSTATUS, 0, RV64_SSTATUS_FIXED);
      system_action(SYSTEM_ECALL, UINT64_C(64), target);
      csr_access(CSR_SET, CSR_SEPC, 0, UINT64_C(64));
      reset_dut();
    }
    for (int index = 2; index < 38; index++) {
      std::uint64_t target =
          UINT64_C(18446743798831644672) | (UINT64_C(1) << index);
      csr_access(CSR_WRITE, CSR_MEDELEG, UINT64_C(1) << 9, 0);
      csr_access(CSR_WRITE, CSR_STVEC, target, 0);
      enter_supervisor(0);
      system_action(SYSTEM_ECALL, UINT64_C(68), target);
      reset_dut();
    }

    csr_access(CSR_WRITE, CSR_MISA, UINT64_C(0), RV64_MISA_DC);
    csr_access(CSR_WRITE, CSR_MISA, ~UINT64_C(0), RV64_MISA_DC);
    csr_access(CSR_SET, CSR_MISA, UINT64_C(0), RV64_MISA_DC);
    for (int widths = 0; widths < 16; widths++) {
      csr_access(CSR_WRITE, CSR_MSTATUS, ((widths)&low_mask(64)) << 32,
                 RV64_MSTATUS_FIXED);
      csr_access(CSR_SET, CSR_MSTATUS, UINT64_C(0), RV64_MSTATUS_FIXED);
      csr_access(CSR_WRITE, CSR_SSTATUS, ((widths & 3) & low_mask(64)) << 32,
                 RV64_SSTATUS_FIXED);
      csr_access(CSR_SET, CSR_SSTATUS, UINT64_C(0), RV64_SSTATUS_FIXED);
    }
    reset_dut();

    csr_access(CSR_WRITE, CSR_MSTATUS, UINT64_C(4113), RV64_MSTATUS_FIXED);
    csr_access(CSR_SET, CSR_MSTATUS, UINT64_C(0), RV64_MSTATUS_FIXED);
    csr_access(CSR_WRITE, CSR_SSTATUS, UINT64_C(16), RV64_SSTATUS_FIXED);
    csr_access(CSR_SET, CSR_SSTATUS, UINT64_C(0), RV64_SSTATUS_FIXED);
    csr_access(CSR_WRITE, CSR_MEDELEG, ~UINT64_C(0), UINT64_C(0));
    csr_access(CSR_SET, CSR_MEDELEG, UINT64_C(0), UINT64_C(832510));
    reset_dut();

    csr_access(CSR_WRITE, CSR_MEDELEG,
               (UINT64_C(1) << 9) | (UINT64_C(1) << 18) | (UINT64_C(1) << 19),
               UINT64_C(0));
    csr_access(CSR_WRITE, CSR_STVEC, UINT64_C(512), UINT64_C(0));
    enter_supervisor(UINT64_C(0));
    system_action(SYSTEM_ECALL, UINT64_C(64), UINT64_C(512));
    csr_access(CSR_SET, CSR_SCAUSE, UINT64_C(0), UINT64_C(9));
    explicit_exception(UINT64_C(18), UINT64_C(68), UINT64_C(512), PRIVILEGE_S);
    csr_access(CSR_SET, CSR_SCAUSE, UINT64_C(0), UINT64_C(18));
    explicit_exception(UINT64_C(19), UINT64_C(72), UINT64_C(512), PRIVILEGE_S);
    csr_access(CSR_SET, CSR_SCAUSE, UINT64_C(0), UINT64_C(19));
    reset_dut();

    csr_access(CSR_WRITE, CSR_MIDELEG, UINT64_C(546), UINT64_C(0));
    csr_access(CSR_WRITE, CSR_SIP, ~UINT64_C(0), UINT64_C(0));
    csr_access(CSR_SET, CSR_SIP, UINT64_C(0), UINT64_C(2));
    csr_access(CSR_WRITE, CSR_MIP, UINT64_C(0), UINT64_C(2));
    reset_dut();

    csr_access(CSR_WRITE, CSR_MENVCFG, UINT64_C(1), UINT64_C(0));
    csr_access(CSR_SET, CSR_MENVCFG, UINT64_C(0), UINT64_C(1));
    csr_access(CSR_WRITE, CSR_SENVCFG, UINT64_C(1), UINT64_C(0));
    csr_access(CSR_SET, CSR_SENVCFG, UINT64_C(0), UINT64_C(1));
    reset_dut();

    CHECK(((pointer_masking.pmode << 1) | pointer_masking.pvirtual_uaddress) ==
              0 &&
          !pointer_masking_changed);
    csr_access(CSR_WRITE, UINT64_C(266), UINT64_C(8589934720), 0);
    csr_access(CSR_SET, UINT64_C(266), 0, UINT64_C(8589934720));
    csr_access(CSR_SET, UINT64_C(266), UINT64_C(4294967296),
               UINT64_C(8589934720));
    csr_access(CSR_CLEAR, UINT64_C(266), UINT64_C(4294967296),
               UINT64_C(12884902016));
    csr_access(CSR_WRITE, CSR_MSTATUS, UINT64_C(131072), RV64_MSTATUS_FIXED);
    CHECK(((pointer_masking.pmode << 1) | pointer_masking.pvirtual_uaddress) ==
          UINT64_C(4));
    csr_access(CSR_SET, CSR_MSTATUS, UINT64_C(524288),
               RV64_MSTATUS_FIXED | UINT64_C(131072));
    CHECK(((pointer_masking.pmode << 1) | pointer_masking.pvirtual_uaddress) ==
          0);
    csr_access(CSR_WRITE, UINT64_C(266), UINT64_C(4294967424),
               UINT64_C(8589934720));
    csr_access(CSR_SET, UINT64_C(266), 0, UINT64_C(128));
    reset_dut();

    enter_supervisor(0);
    csr_access(CSR_WRITE, UINT64_C(266), UINT64_C(12884901888), 0);
    CHECK(((pointer_masking.pmode << 1) | pointer_masking.pvirtual_uaddress) ==
          0);
    csr_access(CSR_WRITE, CSR_SEPC, UINT64_C(0x300), 0);
    system_action(SYSTEM_SRET, 0, UINT64_C(0x300));
    CHECK(privilege == PRIVILEGE_U &&
          ((pointer_masking.pmode << 1) | pointer_masking.pvirtual_uaddress) ==
              UINT64_C(6));
    csr_write_intent_traps(CSR_WRITE, UINT64_C(266), 1);
    CHECK(privilege == PRIVILEGE_M && ((pointer_masking.pmode << 1) |
                                       pointer_masking.pvirtual_uaddress) == 0);
    csr_access(CSR_SET, UINT64_C(266), 0, UINT64_C(12884901888));
    reset_dut();

    csr_access(CSR_WRITE, CSR_MIDELEG, UINT64_C(512), UINT64_C(0));
    csr_access(CSR_WRITE, CSR_MIE, UINT64_C(512), UINT64_C(0));
    interrupts.psupervisor_uexternal = UINT64_C(1);
    eval_csr();
    CHECK(wfi_wake && !interrupt_request && !redirect_out.pvalid);
    reset_dut();

    csr_access(CSR_WRITE, CSR_MCYCLE, UINT64_C(256), UINT64_C(0));
    csr_access(CSR_SET, CSR_CYCLE, UINT64_C(0), UINT64_C(256));
    csr_access(CSR_WRITE, CSR_MINSTRET, UINT64_C(512), UINT64_C(2));
    csr_access(CSR_SET, CSR_INSTRET, UINT64_C(0), UINT64_C(512));
    csr_access(CSR_SET, CSR_MISA, UINT64_C(0), RV64_MISA_DC);
    csr_access(CSR_WRITE, CSR_MEPC, UINT64_C(514), UINT64_C(0));
    csr_access(CSR_SET, CSR_MEPC, UINT64_C(0), UINT64_C(514));
    csr_access(CSR_WRITE, CSR_MEPC, UINT64_C(0), UINT64_C(514));
    csr_access(CSR_WRITE, CSR_SEPC, UINT64_C(258), UINT64_C(0));
    csr_access(CSR_SET, CSR_SEPC, UINT64_C(0), UINT64_C(258));
    csr_access(CSR_WRITE, CSR_SEPC, UINT64_C(0), UINT64_C(258));

    csr_access(CSR_WRITE, CSR_MSCRATCH, UINT64_C(18), UINT64_C(0));
    csr_access(CSR_SET, CSR_MSCRATCH, UINT64_C(1), UINT64_C(18));
    csr_access(CSR_CLEAR, CSR_MSCRATCH, UINT64_C(16), UINT64_C(19));
    csr_access(CSR_SET, CSR_MSCRATCH, UINT64_C(0), UINT64_C(3));
    csr_access(CSR_SET, CSR_TIME, UINT64_C(0), time_counter);
    csr_access(CSR_SET, CSR_MHARTID, UINT64_C(0), hart_id);

    csr_access(CSR_WRITE, CSR_MTVEC, UINT64_C(256), UINT64_C(0));
    csr_access(CSR_WRITE, CSR_STVEC, UINT64_C(512), UINT64_C(0));

    interrupts.pmachine_utimer = UINT64_C(1);
    eval_csr();
    CHECK(!wfi_wake && !interrupt_request && !redirect_out.pvalid);
    csr_access(CSR_SET, CSR_MIP, UINT64_C(0), UINT64_C(128));
    csr_access(CSR_WRITE, CSR_MIE, UINT64_C(128), UINT64_C(0));
    eval_csr();
    CHECK(wfi_wake && !interrupt_request && !redirect_out.pvalid);
    csr_access(CSR_WRITE, CSR_MSTATUS, UINT64_C(8), RV64_MSTATUS_FIXED);
    eval_csr();
    CHECK(wfi_wake && interrupt_request && !redirect_out.pvalid);
    take_interrupt(UINT64_C(96), UINT64_C(256), PRIVILEGE_M);
    csr_access(CSR_SET, CSR_MCAUSE, UINT64_C(0), UINT64_C(9223372036854775815));
    csr_access(CSR_SET, CSR_MEPC, UINT64_C(0), UINT64_C(96));
    csr_access(CSR_SET, CSR_MTVAL, UINT64_C(0), UINT64_C(0));
    system_action(SYSTEM_MRET, UINT64_C(0), UINT64_C(96));
    csr_access(CSR_WRITE, CSR_MSTATUS, UINT64_C(0),
               RV64_MSTATUS_FIXED | UINT64_C(136));

    csr_access(CSR_WRITE, CSR_MIP, UINT64_C(32), UINT64_C(0));
    csr_access(CSR_SET, CSR_MIP, UINT64_C(0), UINT64_C(32));
    csr_access(CSR_WRITE, CSR_MIP, UINT64_C(0), UINT64_C(32));

    csr_access(CSR_WRITE, CSR_MEDELEG, UINT64_C(260), UINT64_C(0));
    csr_access(CSR_WRITE, CSR_MIDELEG, UINT64_C(512), UINT64_C(0));
    csr_access(CSR_WRITE, CSR_MIE, UINT64_C(512), UINT64_C(128));
    csr_access(CSR_WRITE, CSR_MCOUNTEREN, UINT64_C(5), UINT64_C(0));
    csr_access(CSR_WRITE, CSR_SCOUNTEREN, UINT64_C(5), UINT64_C(0));
    csr_access(CSR_WRITE, CSR_MEPC, UINT64_C(128), UINT64_C(96));
    csr_access(CSR_WRITE, CSR_MSTATUS, UINT64_C(2050), RV64_MSTATUS_FIXED);
    system_action(SYSTEM_MRET, UINT64_C(0), UINT64_C(128));
    CHECK(privilege == PRIVILEGE_S);
    csr_read_legal(CSR_CYCLE);
    csr_read_legal(CSR_INSTRET);

    interrupts.psupervisor_uexternal = UINT64_C(1);
    eval_csr();
    CHECK(interrupt_request);
    csr_access(CSR_SET, CSR_SIP, UINT64_C(0), UINT64_C(512));
    take_interrupt(UINT64_C(132), UINT64_C(512), PRIVILEGE_S);
    csr_access(CSR_SET, CSR_SCAUSE, UINT64_C(0), UINT64_C(9223372036854775817));
    csr_access(CSR_SET, CSR_SEPC, UINT64_C(0), UINT64_C(132));
    system_action(SYSTEM_SRET, UINT64_C(0), UINT64_C(132));
    CHECK(privilege == PRIVILEGE_S);
    csr_access(CSR_CLEAR, CSR_SSTATUS, UINT64_C(256),
               RV64_SSTATUS_FIXED | UINT64_C(34));

    csr_access(CSR_WRITE, CSR_SEPC, UINT64_C(64), UINT64_C(132));
    system_action(SYSTEM_SRET, UINT64_C(0), UINT64_C(64));
    CHECK(privilege == PRIVILEGE_U);
    csr_read_legal(CSR_CYCLE);
    csr_read_legal(CSR_INSTRET);

    system_action(SYSTEM_ECALL, UINT64_C(68), UINT64_C(512));
    CHECK(privilege == PRIVILEGE_S);
    csr_access(CSR_SET, CSR_SCAUSE, UINT64_C(0), UINT64_C(8));
    csr_access(CSR_SET, CSR_SEPC, UINT64_C(0), UINT64_C(68));

    system_action(SYSTEM_EBREAK, UINT64_C(72), UINT64_C(256));
    CHECK(privilege == PRIVILEGE_M);
    csr_access(CSR_SET, CSR_MCAUSE, UINT64_C(0), UINT64_C(3));
    csr_access(CSR_SET, CSR_MEPC, UINT64_C(0), UINT64_C(72));

    csr_write_intent_traps(CSR_SET, CSR_CYCLE, UINT64_C(1));

    reset_dut();
    enter_supervisor(UINT64_C(0));
    csr_read_legal(CSR_SATP);
    privileged_action(SYSTEM_NONE, FENCE_ADDRESS_TRANSLATION, UINT64_C(516),
                      UINT64_C(301990003), UINT64_C(0), UINT64_C(1));
    privileged_action(SYSTEM_WFI, FENCE_NONE, UINT64_C(520),
                      UINT64_C(273678451), UINT64_C(0), UINT64_C(0));

    reset_dut();
    enter_supervisor(MSTATUS_TVM);
    privileged_action(SYSTEM_NONE, FENCE_ADDRESS_TRANSLATION, UINT64_C(524),
                      UINT64_C(301990003), UINT64_C(1), UINT64_C(0));
    check_illegal_trap(UINT64_C(524), UINT64_C(301990003));

    reset_dut();
    enter_supervisor(MSTATUS_TVM);
    illegal_csr_read(CSR_SATP, UINT64_C(528), UINT64_C(402661619));
    check_illegal_trap(UINT64_C(528), UINT64_C(402661619));

    reset_dut();
    enter_supervisor(MSTATUS_TSR);
    privileged_action(SYSTEM_SRET, FENCE_NONE, UINT64_C(532),
                      UINT64_C(270532723), UINT64_C(1), UINT64_C(0));
    check_illegal_trap(UINT64_C(532), UINT64_C(270532723));

    reset_dut();
    enter_supervisor(MSTATUS_TW);
    privileged_action(SYSTEM_WFI, FENCE_NONE, UINT64_C(536),
                      UINT64_C(273678451), UINT64_C(1), UINT64_C(0));
    check_illegal_trap(UINT64_C(536), UINT64_C(273678451));

    reset_dut();
    enter_supervisor(UINT64_C(0));
    csr_access(CSR_WRITE, CSR_SEPC, UINT64_C(540), UINT64_C(0));
    system_action(SYSTEM_SRET, UINT64_C(0), UINT64_C(540));
    CHECK(privilege == PRIVILEGE_U);
    privileged_action(SYSTEM_WFI, FENCE_NONE, UINT64_C(540),
                      UINT64_C(273678451), UINT64_C(1), UINT64_C(0));
    check_illegal_trap(UINT64_C(540), UINT64_C(273678451));

    reset_dut();
    csr_access(CSR_WRITE, CSR_MTVEC, UINT64_C(256), UINT64_C(0));
    illegal_csr_read(CSR_FCSR, UINT64_C(544), UINT64_C(3154163));
    check_illegal_trap(UINT64_C(544), UINT64_C(3154163));
    reset_dut();
    csr_access(CSR_WRITE, CSR_MSTATUS, MSTATUS_FS_INITIAL, RV64_MSTATUS_FIXED);
    CHECK(fp_enabled && frm == 0 &&
          mstatus == (RV64_MSTATUS_FIXED | MSTATUS_FS_INITIAL));
    csr_access(CSR_WRITE, CSR_FCSR, UINT64_C(97), UINT64_C(0));
    CHECK(frm == 3 &&
          mstatus == (RV64_MSTATUS_FIXED | MSTATUS_FS_DIRTY | MSTATUS_SD));
    csr_access(CSR_SET, CSR_SSTATUS, UINT64_C(0),
               RV64_SSTATUS_FIXED | MSTATUS_FS_DIRTY | MSTATUS_SD);
    csr_access(CSR_WRITE, CSR_SSTATUS, MSTATUS_FS_INITIAL,
               RV64_SSTATUS_FIXED | MSTATUS_FS_DIRTY | MSTATUS_SD);
    CHECK(fp_enabled && frm == 3 &&
          mstatus == (RV64_MSTATUS_FIXED | MSTATUS_FS_INITIAL));
    csr_access(CSR_SET, CSR_FCSR, UINT64_C(0), UINT64_C(97));
    fp_state_update(UINT64_C(20));
    csr_access(CSR_SET, CSR_FFLAGS, UINT64_C(0), UINT64_C(21));
    csr_access(CSR_SET, CSR_FCSR, UINT64_C(0), UINT64_C(117));
    csr_access(CSR_WRITE, CSR_FRM, UINT64_C(4), UINT64_C(3));
    CHECK(frm == 4);
    csr_access(CSR_SET, CSR_FCSR, UINT64_C(0), UINT64_C(149));
    csr_access(CSR_WRITE, CSR_FFLAGS, UINT64_C(2), UINT64_C(21));
    csr_access(CSR_SET, CSR_FCSR, UINT64_C(0), UINT64_C(130));

    reset_dut();
    CHECK(cbo_zero_access == 0);
    CHECK(!pbmte);
    csr_access(CSR_WRITE, CSR_MENVCFG, ~UINT64_C(0), UINT64_C(0), UINT64_C(1));
    CHECK(pbmte);
    csr_access(CSR_SET, CSR_MENVCFG, UINT64_C(0),
               UINT64_C(4611686018427388033));
    csr_access(CSR_SET, CSR_MENVCFG, UINT64_C(4611686018427387904),
               UINT64_C(4611686018427388033));
    csr_access(CSR_CLEAR, CSR_MENVCFG, UINT64_C(4611686018427387904),
               UINT64_C(4611686018427388033), UINT64_C(1));
    CHECK(!pbmte);
    csr_access(CSR_SET, CSR_MENVCFG, UINT64_C(0), UINT64_C(129));
    csr_access(CSR_WRITE, CSR_SENVCFG, ~UINT64_C(0), UINT64_C(0));
    csr_access(CSR_SET, CSR_SENVCFG, UINT64_C(0), UINT64_C(12884902017));
    enter_supervisor(UINT64_C(0));
    CHECK(cbo_zero_access == 0);
    csr_access(CSR_WRITE, CSR_SENVCFG, UINT64_C(0), UINT64_C(12884902017));
    CHECK(cbo_zero_access == 0);
    csr_access(CSR_WRITE, CSR_SEPC, UINT64_C(768), UINT64_C(0));
    system_action(SYSTEM_SRET, UINT64_C(0), UINT64_C(768));
    CHECK(privilege == PRIVILEGE_U && cbo_zero_access == 1);
    reset_dut();
    csr_access(CSR_WRITE, CSR_MENVCFG, UINT64_C(128), UINT64_C(0));
    csr_access(CSR_WRITE, CSR_SENVCFG, UINT64_C(128), UINT64_C(0));
    csr_access(CSR_WRITE, CSR_MEPC, UINT64_C(768), UINT64_C(0));
    system_action(SYSTEM_MRET, UINT64_C(0), UINT64_C(768));
    CHECK(privilege == PRIVILEGE_U && cbo_zero_access == 0);
    reset_dut();
    csr_access(CSR_WRITE, CSR_SENVCFG, UINT64_C(128), UINT64_C(0));
    enter_supervisor(UINT64_C(0));
    CHECK(cbo_zero_access == 1);
    reset_dut();
    eval_csr();
    clear_commit();
    commit_in.pvalid = UINT64_C(1);
    commit_in.pbits.pcsr_uoperation = CSR_WRITE;
    commit_in.pbits.pcsr_uaddress = CSR_MENVCFG;
    commit_in.pbits.pcsr_usource = UINT64_C(4611686018427387904);
    commit_in.pbits.pexception_uvalid = UINT64_C(1);
    commit_in.pbits.pexception_ucause = UINT64_C(2);
    eval_csr();
    CHECK(!command_success && !translation_flush && redirect_out.pvalid);
    tick_csr();
    clear_commit();
    CHECK(!pbmte);

    reset_dut();
    count_commands = 0;
    csr_access(CSR_WRITE, CSR_MSCRATCH, UINT64_C(85), 0);
    csr_access(CSR_SET, CSR_MINSTRET, 0, 0);
    eval_csr();
    independent_retire = 1;
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
      tick_csr();
    }
    eval_csr();
    independent_retire = 0;
    csr_access(CSR_SET, CSR_INSTRET, 0, 3);
    csr_access(CSR_SET, CSR_MSCRATCH, 0, UINT64_C(85));
    csr_access(CSR_SET, CSR_MINSTRET, 0, 3);
    eval_csr();
    independent_retire = 2;
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
      tick_csr();
    }
    eval_csr();
    independent_retire = 0;
    csr_access(CSR_SET, CSR_MINSTRET, 0, 9);
  });
}
