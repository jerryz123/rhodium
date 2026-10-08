// Exercises the riscv-hypervisor-csr component contract through rsim.
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

constexpr std::uint64_t MPV = UINT64_C(549755813888);
constexpr std::uint64_t GVA = UINT64_C(274877906944);

void clear_commit() {
  commit_in = {};
  guest_fault_in = {};
  interrupt_boundary = 0;
}

void check_context(std::uint8_t mode, bool virtualized) {
  CHECK(privilege == mode && execution_context.pprivilege == mode &&
        execution_context.pvirtualized == virtualized);
}

void csr(bool write, std::uint16_t address, std::uint64_t value,
         std::uint64_t expected = 0, int expected_flush = -1,
         int expected_mask_change = -1) {
  eval_csr();
  clear_commit();
  commit_in.pvalid = 1;
  commit_in.pbits.prd = 1;
  commit_in.pbits.pcsr_uoperation = write ? 1 : 2;
  commit_in.pbits.pcsr_uaddress = address;
  commit_in.pbits.pcsr_usource = value;
  eval_csr();
  if (expected_flush >= 0)
    CHECK(translation_flush == ((expected_flush)&low_mask(1)));
  if (expected_mask_change >= 0)
    CHECK(pointer_masking_changed == ((expected_mask_change)&low_mask(1)));
  CHECK(command_success && writeback_valid && !redirect_out.pvalid);
  if (!write)
    CHECK(writeback_value == expected);
  tick_csr();
  clear_commit();
}

void command(std::uint8_t action, std::uint64_t target, bool traps = 0,
             std::uint64_t pc = UINT64_C(0x804)) {
  eval_csr();
  clear_commit();
  commit_in.pvalid = 1;
  commit_in.pbits.paction = action;
  commit_in.pbits.ppc = pc;
  commit_in.pbits.pinstruction =
      action == 4 ? UINT64_C(270532723) : UINT64_C(115);
  eval_csr();
  CHECK(redirect_out.pvalid && redirect_out.pbits == target &&
        command_success == !traps);
  tick_csr();
  clear_commit();
}

void reset_dut(bool enable_state = 1) {
  eval_csr();
  reset = 1;
  clear_commit();
  interrupts = {};
  fp_update_in = {};
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
    tick_csr();
  reset = 0;
  check_context(3, 0);
  csr(1, UINT64_C(0x305), UINT64_C(0x1000));
  csr(1, UINT64_C(0x105), UINT64_C(0x2000));
  csr(1, UINT64_C(0x205), UINT64_C(0x3000));
  csr(1, UINT64_C(0x302), UINT64_C(0x00ffffff));
  if (enable_state) {
    for (int i = 0; i < 4; i++)
      csr(1, ((UINT64_C(0x30c) + i) & low_mask(12)), UINT64_MAX);
    for (int i = 0; i < 4; i++)
      csr(1, ((UINT64_C(0x60c) + i) & low_mask(12)), UINT64_MAX);
  }
}

void enter_guest(bool user_mode = 0, std::uint8_t host_fs = 0,
                 std::uint8_t host_vs = 0) {
  csr(1, UINT64_C(0x300),
      MPV | (user_mode ? UINT64_C(0) : UINT64_C(2048)) |
          (((host_fs)&low_mask(64)) << 13) | (((host_vs)&low_mask(64)) << 9));
  csr(1, UINT64_C(0x341), UINT64_C(0x800));
  command(3, UINT64_C(0x800));
  check_context(user_mode ? 0 : 1, 1);
  CHECK((mstatus & MPV) == 0);
}

void guest_fault(std::uint64_t cause, bool implicit_pte, bool to_machine,
                 std::uint64_t fault_address = UINT64_C(0x123456780)) {
  eval_csr();
  clear_commit();
  commit_in.pvalid = 1;
  commit_in.pbits.ppc = UINT64_C(0x880);
  commit_in.pbits.pexception_uvalid = 1;
  commit_in.pbits.pexception_ucause = cause;
  commit_in.pbits.pexception_uvalue = UINT64_C(0x12345678);
  guest_fault_in.pvalid = 1;
  guest_fault_in.pbits.pguest_uvirtual_uaddress = 1;
  guest_fault_in.pbits.pguest_uphysical_uaddress = fault_address;
  guest_fault_in.pbits.paccess = implicit_pte;
  eval_csr();
  CHECK(!command_success && redirect_out.pvalid &&
        redirect_out.pbits ==
            (to_machine ? UINT64_C(0x1000) : UINT64_C(0x2000)));
  tick_csr();
  clear_commit();
  check_context(to_machine ? 3 : 1, 0);
  csr(0, to_machine ? UINT64_C(834) : UINT64_C(322), 0, cause);
  csr(0, to_machine ? UINT64_C(835) : UINT64_C(323), 0, UINT64_C(0x12345678));
  csr(0, to_machine ? UINT64_C(843) : UINT64_C(1603), 0, fault_address >> 2);
  csr(0, to_machine ? UINT64_C(842) : UINT64_C(1610), 0,
      implicit_pte ? UINT64_C(0x3000) : 0);
}

void denied_csr(std::uint16_t address, std::uint64_t cause, bool write = 0) {
  eval_csr();
  clear_commit();
  commit_in.pvalid = 1;
  commit_in.pbits.ppc = UINT64_C(0x884);
  commit_in.pbits.prd = 1;
  commit_in.pbits.pcsr_uoperation = write ? 1 : 2;
  commit_in.pbits.pcsr_uaddress = address;
  commit_in.pbits.pcsr_usource = UINT64_C(0xffff);
  commit_in.pbits.pinstruction = ((std::uint32_t(address) << 20) | 8435);
  eval_csr();
  CHECK(!command_success && !writeback_valid && !translation_flush &&
        redirect_out.pvalid && redirect_out.pbits == UINT64_C(0x2000));
  tick_csr();
  clear_commit();
  check_context(1, 0);
  csr(0, UINT64_C(0x142), 0, cause);
  csr(0, UINT64_C(0x143), 0, ((std::uint32_t(address) << 20) | 8435));
}

void fence(std::uint8_t action, bool legal, int cause = 0) {
  eval_csr();
  clear_commit();
  commit_in.pvalid = 1;
  commit_in.pbits.ppc = UINT64_C(0x888);
  commit_in.pbits.paction = ((action + 4) & low_mask(4));
  commit_in.pbits.pinstruction =
      action == 4 ? UINT64_C(570425459) : UINT64_C(1644167283);
  eval_csr();
  CHECK(command_success == legal && translation_flush == legal &&
        redirect_out.pvalid == !legal);
  tick_csr();
  clear_commit();
  if (!legal)
    csr(0, UINT64_C(0x142), 0, ((cause)&low_mask(64)));
}

void take_interrupt(std::uint64_t target, std::uint8_t mode, bool guest,
                    std::uint64_t cause) {
  eval_csr();
  clear_commit();
  interrupt_pc = UINT64_C(0xabc);
  interrupt_boundary = 1;
  eval_csr();
  CHECK(interrupt_request && redirect_out.pvalid &&
        redirect_out.pbits == target);
  tick_csr();
  clear_commit();
  check_context(mode, guest);
  csr(0, mode == 3 ? UINT64_C(834) : UINT64_C(322), 0,
      UINT64_C(9223372036854775808) | cause);
  csr(0, mode == 3 ? UINT64_C(833) : UINT64_C(321), 0, UINT64_C(0xabc));
  csr(0, mode == 3 ? UINT64_C(835) : UINT64_C(323), 0, 0);
}

void virtual_interrupts() {

  reset_dut();
  csr(0, UINT64_C(0x303), 0, UINT64_C(0x444));
  csr(1, UINT64_C(0x303), UINT64_C(0xffff));
  csr(0, UINT64_C(0x303), 0, UINT64_C(0x666));
  csr(1, UINT64_C(0x303), 0);
  csr(1, UINT64_C(0x604), UINT64_C(0xffff));
  csr(0, UINT64_C(0x304), 0, UINT64_C(0x444));
  csr(0, UINT64_C(0x104), 0, 0);
  csr(1, UINT64_C(0x104), UINT64_C(0xffff));
  csr(0, UINT64_C(0x604), 0, UINT64_C(0x444));
  csr(1, UINT64_C(0x645), UINT64_C(0xffff));
  csr(0, UINT64_C(0x644), 0, UINT64_C(0x444));
  csr(0, UINT64_C(0x344), 0, UINT64_C(0x444));
  csr(0, UINT64_C(0x144), 0, 0);
  csr(0, UINT64_C(0x244), 0, 0);
  csr(0, UINT64_C(0x204), 0, 0);
  csr(1, UINT64_C(0x204), 0);
  csr(0, UINT64_C(0x604), 0, UINT64_C(0x444));
  csr(1, UINT64_C(0x603), UINT64_C(0xffff));
  csr(0, UINT64_C(0x603), 0, UINT64_C(0x444));
  csr(0, UINT64_C(0x244), 0, UINT64_C(0x222));
  csr(0, UINT64_C(0x204), 0, UINT64_C(0x222));
  csr(1, UINT64_C(0x644), 0);
  csr(0, UINT64_C(0x645), 0, UINT64_C(0x440));
  csr(1, UINT64_C(0x344), 4);
  csr(0, UINT64_C(0x645), 0, UINT64_C(0x444));
  csr(1, UINT64_C(0x244), 0);
  csr(0, UINT64_C(0x645), 0, UINT64_C(0x440));
  csr(1, UINT64_C(0x244), UINT64_C(0xffff));
  csr(0, UINT64_C(0x645), 0, UINT64_C(0x444));
  csr(1, UINT64_C(0x304), UINT64_C(0xeee));
  csr(1, UINT64_C(0x204), 0);
  csr(0, UINT64_C(0x304), 0, UINT64_C(0xaaa));
  csr(1, UINT64_C(0x204), UINT64_C(0xffff));
  csr(0, UINT64_C(0x304), 0, UINT64_C(0xeee));
  csr(1, UINT64_C(0x607), UINT64_C(0xffff));
  csr(0, UINT64_C(0x607), 0, 0);
  csr(0, UINT64_C(0xe12), 0, 0);

  for (int index = 0; index < 3; index++) {
    reset_dut();
    csr(1, UINT64_C(0x603), UINT64_C(0x444));
    csr(1, UINT64_C(0x604), UINT64_C(1) << (2 + index * 4));
    csr(1, UINT64_C(0x645), UINT64_C(1) << (2 + index * 4));
    csr(1, UINT64_C(0x200), 2);
    enter_guest();
    eval_csr();
    CHECK(interrupt_request && wfi_wake && !redirect_out.pvalid);
    take_interrupt(UINT64_C(0x3000), 1, 1, ((1 + index * 4) & low_mask(64)));
    csr(0, UINT64_C(0x100), 0, UINT64_C(0x200000120));
    csr(0, UINT64_C(0x144), 0, UINT64_C(1) << (1 + index * 4));
    csr(1, UINT64_C(0x144), 0);
    csr(0, UINT64_C(0x144), 0, index == 0 ? 0 : UINT64_C(1) << (1 + index * 4));
    csr(1, UINT64_C(0x104), 0);
    command(4, UINT64_C(0xabc));
    check_context(1, 1);
    eval_csr();
    CHECK(!interrupt_request);
    command(1, UINT64_C(0x2000), 1);
    csr(1, UINT64_C(0x645), 0);
    csr(0, UINT64_C(0x644), 0, 0);
  }

  reset_dut();
  csr(1, UINT64_C(0x603), 4);
  csr(1, UINT64_C(0x604), 4);
  csr(1, UINT64_C(0x645), 4);
  enter_guest();
  eval_csr();
  CHECK(!interrupt_request && wfi_wake);
  csr(1, UINT64_C(0x100), 2);
  take_interrupt(UINT64_C(0x3000), 1, 1, 1);
  reset_dut();
  csr(1, UINT64_C(0x603), 4);
  csr(1, UINT64_C(0x604), 4);
  csr(1, UINT64_C(0x645), 4);
  enter_guest(1);
  take_interrupt(UINT64_C(0x3000), 1, 1, 1);
  csr(0, UINT64_C(0x100), 0, UINT64_C(0x200000000));
  csr(1, UINT64_C(0x144), 0);
  command(4, UINT64_C(0xabc));
  check_context(0, 1);

  reset_dut();
  csr(1, UINT64_C(0x603), UINT64_C(0x444));
  csr(1, UINT64_C(0x604), UINT64_C(0x444));
  csr(1, UINT64_C(0x645), UINT64_C(0x444));
  csr(1, UINT64_C(0x300), UINT64_C(0x802));
  csr(1, UINT64_C(0x341), UINT64_C(0x800));
  command(3, UINT64_C(0x800));
  eval_csr();
  CHECK(!interrupt_request);
  csr(1, UINT64_C(0x603), UINT64_C(0x404));
  take_interrupt(UINT64_C(0x2000), 1, 0, 6);
  reset_dut();
  csr(1, UINT64_C(0x603), UINT64_C(0x404));
  csr(1, UINT64_C(0x604), UINT64_C(0x444));
  csr(1, UINT64_C(0x645), UINT64_C(0x444));
  csr(1, UINT64_C(0x200), 2);
  enter_guest();
  take_interrupt(UINT64_C(0x2000), 1, 0, 6);
  reset_dut();
  csr(1, UINT64_C(0x604), UINT64_C(0x444));
  csr(1, UINT64_C(0x645), UINT64_C(0x444));
  enter_guest();
  take_interrupt(UINT64_C(0x2000), 1, 0, 10);
  csr(1, UINT64_C(0x645), UINT64_C(0x44));
  command(4, UINT64_C(0xabc));
  take_interrupt(UINT64_C(0x2000), 1, 0, 2);
  csr(1, UINT64_C(0x645), UINT64_C(0x40));
  command(4, UINT64_C(0xabc));
  take_interrupt(UINT64_C(0x2000), 1, 0, 6);

  reset_dut();
  csr(1, UINT64_C(0x603), UINT64_C(0x444));
  csr(1, UINT64_C(0x604), UINT64_C(0x444));
  csr(1, UINT64_C(0x645), UINT64_C(0x444));
  csr(1, UINT64_C(0x303), UINT64_C(0x20));
  csr(1, UINT64_C(0x304), UINT64_C(0x464));
  csr(1, UINT64_C(0x200), 2);
  enter_guest();
  interrupts = {0, 0, 1, 0, 0, 0};
  take_interrupt(UINT64_C(0x2000), 1, 0, 5);
  interrupts = {};
  csr(1, UINT64_C(0x645), UINT64_C(0x444));
  command(4, UINT64_C(0xabc));
  take_interrupt(UINT64_C(0x3000), 1, 1, 9);
  reset_dut();
  csr(1, UINT64_C(0x603), UINT64_C(0x444));
  csr(1, UINT64_C(0x604), UINT64_C(0x444));
  csr(1, UINT64_C(0x645), UINT64_C(0x444));
  csr(1, UINT64_C(0x304), UINT64_C(0x4c4));
  csr(1, UINT64_C(0x200), 2);
  enter_guest();
  interrupts = {0, 0, 0, 1, 0, 0};
  take_interrupt(UINT64_C(0x1000), 3, 0, 7);
  interrupts = {};

  reset_dut();
  csr(1, UINT64_C(0x603), 4);
  csr(1, UINT64_C(0x604), 4);
  csr(1, UINT64_C(0x645), 4);
  csr(1, UINT64_C(0x200), 2);
  enter_guest();
  eval_csr();
  clear_commit();
  interrupt_boundary = 1;
  interrupt_pc = UINT64_C(0xccc);
  commit_in.pvalid = 1;
  commit_in.pbits.ppc = UINT64_C(0x888);
  commit_in.pbits.pexception_uvalid = 1;
  commit_in.pbits.pexception_ucause = 2;
  eval_csr();
  CHECK(!command_success && redirect_out.pbits == UINT64_C(0x2000));
  tick_csr();
  clear_commit();
  csr(0, UINT64_C(0x142), 0, 2);
  csr(0, UINT64_C(0x141), 0, UINT64_C(0x888));
  csr(0, UINT64_C(0x645), 0, 4);
}

void supervisor_timers() {

  reset_dut();
  time_counter = 100;
  csr(1, UINT64_C(0x14d), 101);
  csr(1, UINT64_C(0x24d), 151);
  csr(1, UINT64_C(0x605), 50);
  csr(1, UINT64_C(0x60a), UINT64_C(9223372036854775808));
  csr(0, UINT64_C(0x60a), 0, 0);
  csr(1, UINT64_C(0x344), UINT64_C(0x20));
  csr(0, UINT64_C(0x344), 0, UINT64_C(0x20));
  csr(1, UINT64_C(0x30a), UINT64_C(9223372036854775808));
  csr(1, UINT64_C(0x60a), UINT64_C(9223372036854775808));
  csr(0, UINT64_C(0x344), 0, 0);
  csr(0, UINT64_C(0x644), 0, 0);

  interrupts = {0, 0, 1, 0, 0, 0};
  csr(1, UINT64_C(0x344), UINT64_C(0x20));
  csr(0, UINT64_C(0x344), 0, 0);
  time_counter = 101;
  csr(0, UINT64_C(0x344), 0, UINT64_C(0x60));
  csr(0, UINT64_C(0x644), 0, UINT64_C(0x40));
  csr(1, UINT64_C(0x14d), 102);
  csr(1, UINT64_C(0x24d), 152);
  csr(0, UINT64_C(0x344), 0, 0);
  csr(1, UINT64_C(0x645), UINT64_C(0x40));
  csr(0, UINT64_C(0x644), 0, UINT64_C(0x40));
  csr(1, UINT64_C(0x645), 0);
  csr(0, UINT64_C(0x644), 0, 0);
  csr(1, UINT64_C(0x60a), 0);
  time_counter = 102;
  csr(0, UINT64_C(0x344), 0, UINT64_C(0x20));
  csr(1, UINT64_C(0x30a), 0);
  csr(0, UINT64_C(0x344), 0, UINT64_C(0x20));
  interrupts = {};
  csr(1, UINT64_C(0x344), 0);
  csr(0, UINT64_C(0x344), 0, 0);

  csr(1, UINT64_C(0x30a), UINT64_C(9223372036854775808));
  csr(1, UINT64_C(0x60a), UINT64_C(9223372036854775808));
  time_counter = UINT64_C(18446744073709551614);
  csr(1, UINT64_C(0x605), 3);
  csr(1, UINT64_C(0x14d), UINT64_C(18446744073709551615));
  csr(1, UINT64_C(0x24d), 2);
  csr(0, UINT64_C(0x344), 0, 0);
  time_counter = UINT64_C(18446744073709551615);
  csr(0, UINT64_C(0x344), 0, UINT64_C(0x60));
  time_counter = 0;
  csr(1, UINT64_C(0x605), 0);
  csr(0, UINT64_C(0x344), 0, 0);

  for (int gates = 0; gates < 16; gates++) {
    for (int wr = 0; wr < 2; wr++) {
      reset_dut();
      time_counter = 100;
      csr(1, UINT64_C(0x14d), 200);
      csr(1, UINT64_C(0x24d), 300);
      csr(1, UINT64_C(0x30a),
          ((gates >> 0) & 1) ? UINT64_C(9223372036854775808) : 0);
      csr(1, UINT64_C(0x306), ((gates >> 1) & 1) ? 2 : 0);
      csr(1, UINT64_C(0x60a),
          ((gates >> 2) & 1) ? UINT64_C(9223372036854775808) : 0);
      csr(1, UINT64_C(0x606), ((gates >> 3) & 1) ? 2 : 0);
      enter_guest();
      if (!((gates >> 0) & 1) || !((gates >> 1) & 1))
        denied_csr(UINT64_C(0x14d), 2, ((wr)&low_mask(1)));
      else if (!((gates >> 2) & 1) || !((gates >> 3) & 1))
        denied_csr(UINT64_C(0x14d), 22, ((wr)&low_mask(1)));
      else {
        csr(((wr)&low_mask(1)), UINT64_C(0x14d), 400, 300);
        command(1, UINT64_C(0x2000), 1);
        csr(0, UINT64_C(0x14d), 0, 200);
        csr(0, UINT64_C(0x24d), 0, wr != 0 ? 400 : 300);
      }
    }
  }

  reset_dut();
  csr(1, UINT64_C(0x30a), UINT64_C(9223372036854775808));
  csr(1, UINT64_C(0x306), 2);
  csr(1, UINT64_C(0x300), UINT64_C(0x800));
  csr(1, UINT64_C(0x341), UINT64_C(0x800));
  command(3, UINT64_C(0x800));
  csr(1, UINT64_C(0x14d), 200);
  csr(1, UINT64_C(0x24d), 300);
  reset_dut();
  csr(1, UINT64_C(0x30a), UINT64_C(9223372036854775808));
  csr(1, UINT64_C(0x300), UINT64_C(0x800));
  csr(1, UINT64_C(0x341), UINT64_C(0x800));
  command(3, UINT64_C(0x800));
  denied_csr(UINT64_C(0x24d), 2);

  reset_dut();
  csr(1, UINT64_C(0x30a), UINT64_C(9223372036854775808));
  csr(1, UINT64_C(0x306), 2);
  csr(1, UINT64_C(0x60a), UINT64_C(9223372036854775808));
  csr(1, UINT64_C(0x606), 2);
  enter_guest(1);
  denied_csr(UINT64_C(0x14d), 22);
  reset_dut();
  csr(1, UINT64_C(0x30a), UINT64_C(9223372036854775808));
  csr(1, UINT64_C(0x306), 2);
  enter_guest();
  denied_csr(UINT64_C(0x24d), 22);

  reset_dut();
  time_counter = 100;
  csr(1, UINT64_C(0x14d), 1000);
  csr(1, UINT64_C(0x24d), 151);
  csr(1, UINT64_C(0x605), 50);
  csr(1, UINT64_C(0x30a), UINT64_C(9223372036854775808));
  csr(1, UINT64_C(0x306), 2);
  csr(1, UINT64_C(0x60a), UINT64_C(9223372036854775808));
  csr(1, UINT64_C(0x606), 2);
  csr(1, UINT64_C(0x603), UINT64_C(0x40));
  csr(1, UINT64_C(0x604), UINT64_C(0x40));
  enter_guest();
  CHECK(!wfi_wake && !interrupt_request);
  time_counter = 101;
  eval_csr();
  CHECK(wfi_wake && !interrupt_request);
  csr(0, UINT64_C(0x144), 0, UINT64_C(0x20));
  csr(1, UINT64_C(0x100), 2);
  take_interrupt(UINT64_C(0x3000), 1, 1, 5);
  csr(1, UINT64_C(0x14d), 200);
  command(4, UINT64_C(0xabc));
  check_context(1, 1);
  CHECK(!wfi_wake && !interrupt_request);
  reset_dut();
  time_counter = 100;
}

void fp_status() {

  for (int host_fs = 0; host_fs < 4; host_fs++) {
    for (int guest_fs = 0; guest_fs < 4; guest_fs++) {
      reset_dut();
      csr(1, UINT64_C(0x200), ((guest_fs)&low_mask(64)) << 13);
      csr(0, UINT64_C(0x200), 0,
          UINT64_C(8589934592) | (((guest_fs)&low_mask(64)) << 13) |
              (guest_fs == 3 ? UINT64_C(9223372036854775808) : 0));
      enter_guest(0, ((host_fs)&low_mask(2)));
      eval_csr();
      CHECK(fp_enabled == (host_fs != 0 && guest_fs != 0));
      CHECK(slice(mstatus, 14, 13) == ((host_fs)&low_mask(2)) &&
            ((mstatus >> 63) & 1) == (host_fs == 3));
      csr(0, UINT64_C(0x100), 0,
          UINT64_C(8589934592) | (((guest_fs)&low_mask(64)) << 13) |
              (guest_fs == 3 ? UINT64_C(9223372036854775808) : 0));
      if (host_fs == 0 || guest_fs == 0)
        denied_csr(UINT64_C(0x003), 2);
      else
        csr(0, UINT64_C(0x003), 0, 0);
    }
  }

  reset_dut();
  csr(1, UINT64_C(0x200), UINT64_C(0x4000));
  enter_guest(0, 1);
  csr(1, UINT64_C(0x002), 3);
  csr(0, UINT64_C(0x003), 0, UINT64_C(0x60));
  csr(0, UINT64_C(0x100), 0, UINT64_C(9223372045444734976));
  CHECK(slice(mstatus, 14, 13) == 3 && ((mstatus >> 63) & 1));
  csr(1, UINT64_C(0x100), UINT64_C(0x2000));
  csr(0, UINT64_C(0x100), 0, UINT64_C(0x200002000));
  CHECK(slice(mstatus, 14, 13) == 3 && ((mstatus >> 63) & 1));
  eval_csr();
  fp_update_in = {UINT64_C(1), UINT64_C(5)};
  tick_csr();
  fp_update_in = {};
  csr(0, UINT64_C(0x003), 0, UINT64_C(0x65));
  csr(0, UINT64_C(0x100), 0, UINT64_C(9223372045444734976));
  command(1, UINT64_C(0x2000), 1);
  csr(0, UINT64_C(0x003), 0, UINT64_C(0x65));
  csr(0, UINT64_C(0x200), 0, UINT64_C(9223372045444734976));
  csr(1, UINT64_C(0x200), UINT64_C(0x2000));
  csr(1, UINT64_C(0x100), UINT64_C(0x4100));
  csr(1, UINT64_C(0x001), 0);
  csr(0, UINT64_C(0x200), 0, UINT64_C(0x200002000));
  command(4, UINT64_C(0x804));
  check_context(1, 1);
  csr(0, UINT64_C(0x003), 0, UINT64_C(0x60));
  csr(0, UINT64_C(0x100), 0, UINT64_C(0x200002000));

  reset_dut();
  csr(1, UINT64_C(0x200), UINT64_C(0x2000));
  enter_guest(1, 1);
  csr(1, UINT64_C(0x001), 1);
  csr(0, UINT64_C(0x003), 0, 1);
  command(1, UINT64_C(0x2000), 1);
  csr(0, UINT64_C(0x200), 0, UINT64_C(9223372045444734976));
}

void vector_status() {
  for (int host_vs = 0; host_vs < 4; host_vs++) {
    for (int guest_vs = 0; guest_vs < 4; guest_vs++) {
      reset_dut();
      csr(1, UINT64_C(0x200), ((guest_vs)&low_mask(64)) << 9);
      enter_guest(0, 0, ((host_vs)&low_mask(2)));
      eval_csr();
      CHECK(vector_enabled == (host_vs != 0 && guest_vs != 0));
      CHECK(slice(mstatus, 10, 9) == ((host_vs)&low_mask(2)) &&
            ((mstatus >> 63) & 1) == (host_vs == 3));
      csr(0, UINT64_C(0x100), 0,
          UINT64_C(8589934592) | (((guest_vs)&low_mask(64)) << 9) |
              (guest_vs == 3 ? UINT64_C(9223372036854775808) : 0));
      if (host_vs == 0 || guest_vs == 0)
        denied_csr(UINT64_C(0xc22), 2);
      else
        csr(0, UINT64_C(0xc22), 0, 16);
    }
  }
  reset_dut();
  csr(1, UINT64_C(0x200), UINT64_C(0x200));
  enter_guest(0, 0, 1);
  csr(1, UINT64_C(0x00a), 2);
  csr(0, UINT64_C(0x00f), 0, 4);
  csr(0, UINT64_C(0x100), 0, UINT64_C(9223372045444711936));
  CHECK(slice(mstatus, 10, 9) == 3 && !fp_enabled);
  csr(1, UINT64_C(0x100), UINT64_C(0x200));
  csr(0, UINT64_C(0x100), 0, UINT64_C(0x200000200));
  CHECK(slice(mstatus, 10, 9) == 3 && ((mstatus >> 63) & 1));
  command(1, UINT64_C(0x2000), 1);
  csr(0, UINT64_C(0x00f), 0, 4);
  csr(1, UINT64_C(0x00a), 1);
  csr(0, UINT64_C(0x200), 0, UINT64_C(0x200000200));
  command(4, UINT64_C(0x804));
  csr(0, UINT64_C(0x00f), 0, 2);
  reset_dut();
  csr(1, UINT64_C(0x200), UINT64_C(0x200));
  enter_guest(1, 0, 1);
  csr(1, UINT64_C(0x008), 3);
  csr(0, UINT64_C(0x008), 0, 3);
  command(1, UINT64_C(0x2000), 1);
  csr(0, UINT64_C(0x200), 0, UINT64_C(9223372045444711936));
}

void environment_controls() {
  reset_dut();
  csr(1, UINT64_C(0x30a), UINT64_C(4611686018427387904), 0, 1);
  csr(1, UINT64_C(0x60a), UINT64_C(4611686018427388145), 0, 1);
  csr(0, UINT64_C(0x60a), 0, UINT64_C(4611686018427388145));
  CHECK(pbmte && guest_translation.pvs_upbmte);
  csr(1, UINT64_C(0x60a), UINT64_C(4611686018427388145), 0, 0);
  csr(1, UINT64_C(0x30a), 0, 0, 1);
  csr(0, UINT64_C(0x60a), 0, UINT64_C(0xf1));
  CHECK(!pbmte && !guest_translation.pvs_upbmte);
  csr(1, UINT64_C(0x30a), UINT64_C(4611686018427387904), 0, 1);
  csr(1, UINT64_C(0x60a), UINT64_C(0xf1), 0, 1);
  CHECK(pbmte && !guest_translation.pvs_upbmte);
  reset_dut();
  csr(0, UINT64_C(0x60a), 0, 0);
  csr(1, UINT64_C(0x60a), UINT64_C(18446744073709551615));
  csr(0, UINT64_C(0x60a), 0, UINT64_C(12884902129));
  csr(1, UINT64_C(0x60a), UINT64_C(0xe1));
  csr(0, UINT64_C(0x60a), 0, UINT64_C(0xc1));
  csr(1, UINT64_C(0x60a), UINT64_C(0xf1));
  csr(1, UINT64_C(0x30a), 0);
  csr(0, UINT64_C(0x60a), 0, UINT64_C(0xf1));
  csr(1, UINT64_C(0x30a), UINT64_C(0xf1));
  csr(1, UINT64_C(0x10a), UINT64_C(0xf1));
  csr(1, UINT64_C(0x60a), 0);
  csr(1, UINT64_C(0x300), UINT64_C(0x800));
  csr(1, UINT64_C(0x341), UINT64_C(0x800));
  command(3, UINT64_C(0x800));
  CHECK(cbo_permission.paccess == 0 && cbo_zero_access == 0);
  csr(1, UINT64_C(0x60a), UINT64_C(0xf1));
  csr(0, UINT64_C(0x60a), 0, UINT64_C(0xf1));

  reset_dut();
  csr(1, UINT64_C(0x30a), UINT64_C(0xf1));
  csr(1, UINT64_C(0x60a), 0);
  enter_guest();
  CHECK(cbo_permission.paccess == 2 && cbo_zero_access == 2);
  denied_csr(UINT64_C(0x60a), 22);

  reset_dut();
  csr(1, UINT64_C(0x30a), 0);
  csr(1, UINT64_C(0x60a), 0);
  enter_guest();
  CHECK(cbo_permission.paccess == 1 && cbo_zero_access == 1);

  reset_dut();
  csr(1, UINT64_C(0x30a), UINT64_C(0xf1));
  csr(1, UINT64_C(0x60a), UINT64_C(0xf1));
  csr(1, UINT64_C(0x10a), 0);
  enter_guest();
  CHECK(cbo_permission.paccess == 0 && cbo_zero_access == 0);
  csr(1, UINT64_C(0x10a), UINT64_C(0xd1));
  csr(0, UINT64_C(0x10a), 0, UINT64_C(0xd1));
  command(1, UINT64_C(0x2000), 1);
  csr(0, UINT64_C(0x10a), 0, UINT64_C(0xd1));

  reset_dut();
  csr(1, UINT64_C(0x30a), UINT64_C(0xf1));
  csr(1, UINT64_C(0x60a), UINT64_C(0xf1));
  csr(1, UINT64_C(0x10a), 0);
  enter_guest(1);
  CHECK(cbo_permission.paccess == 2 && cbo_zero_access == 2);
  reset_dut();
  csr(1, UINT64_C(0x30a), UINT64_C(0xf1));
  csr(1, UINT64_C(0x60a), UINT64_C(0xd1));
  csr(1, UINT64_C(0x10a), UINT64_C(0xf1));
  enter_guest(1);
  CHECK(((cbo_permission.paccess << 2) | cbo_permission.poperation) == 2 &&
        cbo_zero_access == 0);
}

void pointer_controls() {
  reset_dut();
  for (int mode = 0; mode < 4; mode++) {
    int legal_mode = mode == 1 ? 0 : mode;
    csr(1, UINT64_C(0x60a), ((mode)&low_mask(64)) << 32);
    csr(0, UINT64_C(0x60a), 0, ((legal_mode)&low_mask(64)) << 32);
    csr(1, UINT64_C(0x600), ((mode)&low_mask(64)) << 48);
    csr(0, UINT64_C(0x600), 0,
        (((legal_mode)&low_mask(64)) << 48) | UINT64_C(8589934592));
  }
  csr(1, UINT64_C(0x60a), UINT64_C(8589934592), 0, 0, 1);
  csr(1, UINT64_C(0x60a), UINT64_C(8589934592), 0, 0, 0);
  csr(1, UINT64_C(0x600), UINT64_C(562949953421568), 0, 0, 1);
  csr(1, UINT64_C(0x600), UINT64_C(562949953421568), 0, 0, 0);
  csr(1, UINT64_C(0x10a), UINT64_C(12884901888), 0, 0, 1);
  csr(1, UINT64_C(0x280), UINT64_C(9223372036854775816));
  enter_guest();
  CHECK(((pointer_masking.pmode << 1) | pointer_masking.pvirtual_uaddress) ==
        UINT64_C(5));
  csr(1, UINT64_C(0x10a), UINT64_C(8589934592), 0, 0, 1);
  csr(0, UINT64_C(0x10a), 0, UINT64_C(8589934592));

  command(1, UINT64_C(0x2000), 1);
  csr(0, UINT64_C(0x10a), 0, UINT64_C(8589934592));
  csr(1, UINT64_C(0x600), UINT64_C(844424930132480), 0, 0, 1);
  csr(1, UINT64_C(0x100), 0);
  csr(1, UINT64_C(0x141), UINT64_C(0x800));
  command(4, UINT64_C(0x800));
  check_context(0, 0);
  CHECK(((pointer_masking.pmode << 1) | pointer_masking.pvirtual_uaddress) ==
            UINT64_C(4) &&
        ((guest_pointer_masking.pmode << 1) |
         guest_pointer_masking.pvirtual_uaddress) == UINT64_C(7));
}

void state_enables() {
  std::uint64_t mask;
  reset_dut(0);
  for (int i = 0; i < 4; i++) {
    mask =
        i == 0 ? UINT64_C(13835058055282163712) : UINT64_C(9223372036854775808);
    csr(0, ((UINT64_C(0x30c) + i) & low_mask(12)), 0, 0);
    csr(0, ((UINT64_C(0x60c) + i) & low_mask(12)), 0, 0);
    csr(1, ((UINT64_C(0x60c) + i) & low_mask(12)), UINT64_MAX);
    csr(0, ((UINT64_C(0x60c) + i) & low_mask(12)), 0, 0);
    csr(1, ((UINT64_C(0x30c) + i) & low_mask(12)), UINT64_MAX);
    csr(0, ((UINT64_C(0x30c) + i) & low_mask(12)), 0, mask);
    csr(0, ((UINT64_C(0x60c) + i) & low_mask(12)), 0, 0);
    csr(1, ((UINT64_C(0x60c) + i) & low_mask(12)), UINT64_MAX);
    csr(0, ((UINT64_C(0x60c) + i) & low_mask(12)), 0, mask);
    csr(1, ((UINT64_C(0x30c) + i) & low_mask(12)), 0);
    csr(0, ((UINT64_C(0x60c) + i) & low_mask(12)), 0, 0);
    csr(1, ((UINT64_C(0x30c) + i) & low_mask(12)), mask);
    csr(0, ((UINT64_C(0x60c) + i) & low_mask(12)), 0, mask);
    csr(1, ((UINT64_C(0x10c) + i) & low_mask(12)), UINT64_MAX);
    csr(0, ((UINT64_C(0x10c) + i) & low_mask(12)), 0, 0);
  }

  for (int i = 0; i < 4; i++) {
    for (int gates = 0; gates < 4; gates++) {
      for (int wr = 0; wr < 2; wr++) {
        reset_dut(0);
        csr(1, ((UINT64_C(0x30c) + i) & low_mask(12)),
            UINT64_C(9223372036854775808));
        csr(1, ((UINT64_C(0x60c) + i) & low_mask(12)),
            ((gates >> 1) & 1) ? UINT64_C(9223372036854775808) : 0);
        csr(1, ((UINT64_C(0x30c) + i) & low_mask(12)),
            ((gates >> 0) & 1) ? UINT64_C(9223372036854775808) : 0);
        enter_guest();
        if (!((gates >> 0) & 1))
          denied_csr(((UINT64_C(0x10c) + i) & low_mask(12)), 2,
                     ((wr)&low_mask(1)));
        else if (!((gates >> 1) & 1))
          denied_csr(((UINT64_C(0x10c) + i) & low_mask(12)), 22,
                     ((wr)&low_mask(1)));
        else
          csr(((wr)&low_mask(1)), ((UINT64_C(0x10c) + i) & low_mask(12)),
              UINT64_MAX, 0);
      }
    }
    reset_dut(0);
    enter_guest();
    denied_csr(((UINT64_C(0x60c) + i) & low_mask(12)), 2);
    reset_dut();
    enter_guest();
    denied_csr(((UINT64_C(0x60c) + i) & low_mask(12)), 22);
    reset_dut(0);
    csr(1, UINT64_C(0x300), UINT64_C(0x800));
    csr(1, UINT64_C(0x341), UINT64_C(0x800));
    command(3, UINT64_C(0x800));
    denied_csr(((UINT64_C(0x60c) + i) & low_mask(12)), 2);
  }

  for (int gates = 0; gates < 4; gates++) {
    for (int wr = 0; wr < 2; wr++) {
      reset_dut();
      csr(1, UINT64_C(0x10a), 1);
      csr(1, UINT64_C(0x60c),
          ((gates >> 1) & 1) ? UINT64_C(4611686018427387904) : 0);
      csr(1, UINT64_C(0x30c),
          ((gates >> 0) & 1) ? UINT64_C(4611686018427387904) : 0);
      enter_guest();
      if (!((gates >> 0) & 1))
        denied_csr(UINT64_C(0x10a), 2, ((wr)&low_mask(1)));
      else if (!((gates >> 1) & 1))
        denied_csr(UINT64_C(0x10a), 22, ((wr)&low_mask(1)));
      else {
        csr(((wr)&low_mask(1)), UINT64_C(0x10a), 0, 1);
        command(1, UINT64_C(0x2000), 1);
      }
      if (((gates >> 0) & 1))
        csr(0, UINT64_C(0x10a), 0, (gates == 3 && wr != 0) ? 0 : 1);
    }
  }

  reset_dut();
  csr(1, UINT64_C(0x30c), UINT64_C(9223372036854775808));
  csr(0, UINT64_C(0x60c), 0, UINT64_C(9223372036854775808));
  csr(1, UINT64_C(0x60a), 1);
  csr(0, UINT64_C(0x60a), 0, 1);
  enter_guest();
  denied_csr(UINT64_C(0x60a), 2);
  reset_dut();
  enter_guest();
  denied_csr(UINT64_C(0x60a), 22);
  reset_dut();
  enter_guest(1);
  denied_csr(UINT64_C(0x10a), 22);
  reset_dut(0);
  enter_guest(1);
  denied_csr(UINT64_C(0x10a), 2);
  reset_dut();
  enter_guest(1);
  denied_csr(UINT64_C(0x10c), 22);
  reset_dut(0);
  enter_guest(1);
  denied_csr(UINT64_C(0x10c), 2);

  reset_dut(0);
  csr(1, UINT64_C(0x200), UINT64_C(0x2200));
  enter_guest(0, 1, 1);
  csr(1, UINT64_C(0x003), 1);
  csr(0, UINT64_C(0x003), 0, 1);
  csr(0, UINT64_C(0xc22), 0, 16);
  reset_dut();
}

void invalidation_permissions() {
  std::uint32_t instruction;
  std::uint8_t action;
  bool guest, user_mode, denied;
  for (int op = 0; op < 5; op++) {
    switch (op) {
    case 0: {
      instruction = UINT64_C(0x16000073);
      action = 3;
    }

    break;
    case 1: {
      instruction = UINT64_C(0x18000073);
      action = 6;
    }

    break;
    case 2: {
      instruction = UINT64_C(0x18100073);
      action = 6;
    }

    break;
    case 3: {
      instruction = UINT64_C(0x26000073);
      action = 4;
    }

    break;
    case 4: {
      instruction = UINT64_C(0x66000073);
      action = 5;
    }

    break;
    }
    for (int mode = 0; mode < 5; mode++) {
      for (int controls = 0; controls < 4; controls++) {
        guest = mode >= 3;
        user_mode = (mode == 2 || mode == 4);
        denied = mode != 0 && (user_mode || (guest && op >= 3) ||
                               (op == 0 && (guest ? ((controls >> 1) & 1)
                                                  : ((controls >> 0) & 1))) ||
                               (op == 4 && !guest && ((controls >> 0) & 1)));
        reset_dut(0);
        csr(1, UINT64_C(0x600), ((controls >> 1) & 1) ? UINT64_C(0x100000) : 0);
        csr(1, UINT64_C(0x300),
            (guest ? MPV : 0) |
                (user_mode   ? 0
                 : mode == 0 ? UINT64_C(0x1800)
                             : UINT64_C(0x800)) |
                (((controls >> 0) & 1) ? UINT64_C(0x100000) : 0));
        if (mode != 0) {
          csr(1, UINT64_C(0x341), UINT64_C(0x800));
          command(3, UINT64_C(0x800));
        }
        eval_csr();
        clear_commit();
        commit_in.pvalid = 1;
        commit_in.pbits.ppc = UINT64_C(0x888);
        commit_in.pbits.pinstruction = instruction;
        commit_in.pbits.paction = ((action + 4) & low_mask(4));
        eval_csr();
        CHECK(command_success == !denied && !writeback_valid &&
              redirect_out.pvalid == denied &&
              translation_flush == (!denied && action != 6));
        tick_csr();
        clear_commit();
        if (denied) {
          csr(0, UINT64_C(0x142), 0, guest ? 22 : 2);
          csr(0, UINT64_C(0x141), 0, UINT64_C(0x888));
          csr(0, UINT64_C(0x143), 0, ((instruction)&low_mask(64)));
        }
      }
    }
  }
}

void sha_csr_contracts() {
  std::uint64_t replacement, expected, vector_base, cause, value;
  reset_dut();
  csr(0, UINT64_C(0x301), 0, UINT64_C(9223372036858188203));
  csr(1, UINT64_C(0x301), 0);
  csr(0, UINT64_C(0x301), 0, UINT64_C(9223372036858188203));

  csr(1, UINT64_C(0x606), UINT64_MAX);
  csr(0, UINT64_C(0x606), 0, 7);
  csr(1, UINT64_C(0x606), 0);
  csr(0, UINT64_C(0x606), 0, 0);
  for (int index = 3; index < 32; index++) {
    csr(1, ((UINT64_C(0xb00) + index) & low_mask(12)), UINT64_MAX);
    csr(0, ((UINT64_C(0xb00) + index) & low_mask(12)), 0, 0);
  }

  for (int mode = 0; mode < 16; mode++) {
    for (int bank = 0; bank < 3; bank++) {
      csr(1,
          bank == 0   ? UINT64_C(384)
          : bank == 1 ? UINT64_C(640)
                      : UINT64_C(1664),
          UINT64_C(9223372036854780468));
      replacement =
          (((mode)&low_mask(64)) << 60) | UINT64_C(1152903912726222459);
      expected = (mode == 0 || mode == 8)
                     ? (((mode)&low_mask(64)) << 60) | UINT64_C(305419899)
                     : UINT64_C(9223372036854780468);
      if (bank == 2)
        expected &= ~UINT64_C(3);
      csr(1,
          bank == 0   ? UINT64_C(384)
          : bank == 1 ? UINT64_C(640)
                      : UINT64_C(1664),
          replacement);
      csr(0,
          bank == 0   ? UINT64_C(384)
          : bank == 1 ? UINT64_C(640)
                      : UINT64_C(1664),
          0, expected);
    }
  }

  for (int bit_index = 2; bit_index <= 38; bit_index++) {
    reset_dut();
    vector_base = bit_index == 38 ? UINT64_C(18446743798831644672)
                                  : UINT64_C(1) << bit_index;
    csr(1, UINT64_C(0x205), vector_base);
    csr(0, UINT64_C(0x205), 0, vector_base);
    csr(1, UINT64_C(0x602), 1 << 8);
    enter_guest(1);
    command(1, vector_base, 1);
    check_context(1, 1);
  }

  for (int index = 0; index < 11; index++) {
    constexpr unsigned causes[] = {0, 1, 2, 3, 4, 5, 6, 7, 12, 13, 15};
    cause = causes[index];
    value = cause == 2 ? UINT64_C(4294967295) : UINT64_C(18446743799137064570);
    reset_dut();
    csr(1, UINT64_C(0x143), UINT64_C(0x123));
    csr(1, UINT64_C(0x602), UINT64_C(1) << cause);
    enter_guest();
    eval_csr();
    clear_commit();
    commit_in.pvalid = 1;
    commit_in.pbits.ppc = UINT64_C(0x888);
    commit_in.pbits.pexception_uvalid = 1;
    commit_in.pbits.pexception_ucause = cause;
    commit_in.pbits.pexception_uvalue = value;
    eval_csr();
    CHECK(!command_success && redirect_out.pvalid &&
          redirect_out.pbits == UINT64_C(0x3000));
    tick_csr();
    clear_commit();
    check_context(1, 1);
    csr(0, UINT64_C(0x142), 0, cause);
    csr(0, UINT64_C(0x143), 0, value);
    csr(0, UINT64_C(0x141), 0, UINT64_C(0x888));
    command(1, UINT64_C(0x2000), 1);
    csr(0, UINT64_C(0x243), 0, value);
    csr(0, UINT64_C(0x241), 0, UINT64_C(0x888));
  }

  for (int index = 0; index < 3; index++)
    for (int controls = 0; controls < 4; controls++) {
      reset_dut();
      if (((controls >> 1) & 1))
        csr(1, UINT64_C(0x302), 0);
      enter_guest();
      guest_fault(index == 0   ? 20
                  : index == 1 ? 21
                               : 23,
                  ((controls >> 0) & 1), ((controls >> 1) & 1),
                  UINT64_C(2199023251448));
    }
}

int main() {
  return run_test([] {
    reset = 1;
    interrupts = {};
    time_counter = 100;
    hart_id = 0;
    interrupt_boundary = 0;
    interrupt_pc = 0;
    fp_update_in = {};
    cbo_operation = 0;

    reset_dut();
    csr(0, UINT64_C(0x301), 0, UINT64_C(9223372036858188203));
    csr(1, UINT64_C(0x301), 0);
    csr(0, UINT64_C(0x301), 0, UINT64_C(9223372036858188203));
    csr(1, UINT64_C(0x301), ~UINT64_C(0));
    csr(0, UINT64_C(0x301), 0, UINT64_C(9223372036858188203));
    sha_csr_contracts();
    invalidation_permissions();
    state_enables();
    supervisor_timers();
    pointer_controls();
    environment_controls();
    vector_status();
    fp_status();
    virtual_interrupts();

    reset_dut();
    csr(1, UINT64_C(0x280), UINT64_C(10376275949580998264));
    csr(0, UINT64_C(0x280), 0, UINT64_C(9223372037160195704));
    csr(1, UINT64_C(0x280), UINT64_C(10376293541461622784));
    csr(0, UINT64_C(0x280), 0, UINT64_C(9223372037160195704));
    csr(1, UINT64_C(0x680), UINT64_C(10376275949580998267));
    csr(0, UINT64_C(0x680), 0, UINT64_C(9223372037160195704));
    csr(1, UINT64_C(0x680), UINT64_C(10376293541461622784));
    csr(0, UINT64_C(0x680), 0, UINT64_C(9223372037160195704));
    fence(4, 1);
    fence(5, 1);

    csr(1, UINT64_C(0x300), UINT64_C(0x100800));
    csr(1, UINT64_C(0x341), UINT64_C(0x800));
    command(3, UINT64_C(0x800));
    fence(4, 1);
    fence(5, 0, 2);
    reset_dut();
    enter_guest();
    fence(4, 0, 22);
    reset_dut();
    enter_guest();
    fence(5, 0, 22);

    reset_dut();
    csr(1, UINT64_C(0x140), UINT64_C(0x1111));
    csr(1, UINT64_C(0x240), UINT64_C(0x2222));
    csr(1, UINT64_C(0x300), UINT64_C(0x800));
    csr(1, UINT64_C(0x341), UINT64_C(0x400));
    command(3, UINT64_C(0x400));
    check_context(1, 0);
    csr(0, UINT64_C(0x600), 0, UINT64_C(0x200000000));
    csr(1, UINT64_C(0x600), UINT64_C(0x180));
    csr(1, UINT64_C(0x100), UINT64_C(0x100));
    csr(1, UINT64_C(0x141), UINT64_C(0x800));
    command(4, UINT64_C(0x800));
    check_context(1, 1);
    csr(0, UINT64_C(0x140), 0, UINT64_C(0x2222));
    csr(1, UINT64_C(0x140), UINT64_C(0x3333));
    csr(1, UINT64_C(0x180), UINT64_C(0xffffffffffffffff));
    csr(0, UINT64_C(0x180), 0, 0);
    command(1, UINT64_C(0x2000), 1);
    check_context(1, 0);
    csr(0, UINT64_C(0x142), 0, 10);
    csr(0, UINT64_C(0x140), 0, UINT64_C(0x1111));
    csr(0, UINT64_C(0x240), 0, UINT64_C(0x3333));
    csr(0, UINT64_C(0x600), 0, UINT64_C(0x200000180));
    command(4, UINT64_C(0x804));
    check_context(1, 1);

    reset_dut();
    csr(1, UINT64_C(0x602), UINT64_C(0xffffff));

    csr(0, UINT64_C(0x602), 0, UINT64_C(0xcb1ff));
    csr(1, UINT64_C(0x200), 2);
    enter_guest(1);
    command(1, UINT64_C(0x3000), 1);
    check_context(1, 1);
    csr(0, UINT64_C(0x142), 0, 8);
    csr(0, UINT64_C(0x141), 0, UINT64_C(0x804));
    csr(0, UINT64_C(0x100), 0, UINT64_C(0x200000020));
    command(4, UINT64_C(0x804));
    check_context(0, 1);
    command(4, UINT64_C(0x2000), 1);
    check_context(1, 0);
    csr(0, UINT64_C(0x142), 0, 22);
    csr(0, UINT64_C(0x143), 0, UINT64_C(0x10200073));

    reset_dut();
    csr(1, UINT64_C(0x600), UINT64_C(0x100000));
    csr(1, UINT64_C(0x300), MPV | UINT64_C(0x700800));
    csr(1, UINT64_C(0x341), UINT64_C(0x800));
    command(3, UINT64_C(0x800));
    eval_csr();
    clear_commit();
    commit_in.pvalid = 1;
    commit_in.pbits.prd = 1;
    commit_in.pbits.pcsr_uoperation = 2;
    commit_in.pbits.pcsr_uaddress = UINT64_C(0x180);
    commit_in.pbits.pinstruction = UINT64_C(0x180020f3);
    eval_csr();
    CHECK(!command_success && !writeback_valid && !translation_flush &&
          redirect_out.pbits == UINT64_C(0x2000));
    tick_csr();
    clear_commit();
    csr(0, UINT64_C(0x142), 0, 22);
    csr(0, UINT64_C(0x143), 0, UINT64_C(0x180020f3));

    reset_dut();
    enter_guest();
    guest_fault(21, 1, 0);
    csr(0, UINT64_C(0x600), 0, UINT64_C(0x2000001c0));
    command(4, UINT64_C(0x880));
    check_context(1, 1);
    reset_dut();
    csr(1, UINT64_C(0x302), 0);
    enter_guest();
    guest_fault(23, 0, 1);
    CHECK((mstatus & (MPV | GVA)) == (MPV | GVA));
    command(3, UINT64_C(0x880));
    check_context(1, 1);

    reset_dut();
    csr(1, UINT64_C(0x306), 7);
    csr(1, UINT64_C(0x606), 7);
    csr(1, UINT64_C(0x605), 25);
    enter_guest();
    csr(0, UINT64_C(0xc01), 0, 125);
    command(1, UINT64_C(0x2000), 1);
    csr(0, UINT64_C(0xc01), 0, 100);
    reset_dut();
    csr(1, UINT64_C(0x306), 7);
    enter_guest();
    denied_csr(UINT64_C(0xc01), 22);
    reset_dut();
    enter_guest();
    denied_csr(UINT64_C(0xc01), 2);
    reset_dut();
    enter_guest();
    denied_csr(UINT64_C(0x600), 22);
    reset_dut();
    enter_guest(1);
    denied_csr(UINT64_C(0x200), 22);
    reset_dut();
    enter_guest();
    denied_csr(UINT64_C(0x300), 2);
    reset_dut();
    enter_guest();
    denied_csr(UINT64_C(0x6ff), 2);
    reset_dut();
    csr(1, UINT64_C(0x306), 7);
    enter_guest();
    denied_csr(UINT64_C(0xc01), 2, 1);
    reset_dut();
    csr(1, UINT64_C(0x306), 7);
    enter_guest();
    denied_csr(UINT64_C(0xc81), 2);

    reset_dut();
    csr(1, UINT64_C(0x600), UINT64_C(0x400000));
    enter_guest();
    command(4, UINT64_C(0x2000), 1);
    csr(0, UINT64_C(0x142), 0, 22);
    reset_dut();
    csr(1, UINT64_C(0x241), UINT64_C(0x900));
    csr(1, UINT64_C(0x300), MPV | UINT64_C(0x400800));
    csr(1, UINT64_C(0x341), UINT64_C(0x800));
    command(3, UINT64_C(0x800));
    command(4, UINT64_C(0x900));
    check_context(0, 1);

    reset_dut();
    csr(1, UINT64_C(0x303), UINT64_C(0x20));
    csr(1, UINT64_C(0x304), UINT64_C(0x20));
    enter_guest();
    eval_csr();
    clear_commit();
    interrupts = {0, 0, 1, 0, 0, 0};
    interrupt_boundary = 1;
    interrupt_pc = UINT64_C(0xabc);
    eval_csr();
    CHECK(interrupt_request && redirect_out.pvalid &&
          redirect_out.pbits == UINT64_C(0x2000));
    tick_csr();
    clear_commit();
    interrupts = {};
    check_context(1, 0);
    csr(0, UINT64_C(0x141), 0, UINT64_C(0xabc));
    csr(0, UINT64_C(0x142), 0, UINT64_C(0x8000000000000005));
    csr(0, UINT64_C(0x643), 0, 0);
    csr(0, UINT64_C(0x64a), 0, 0);

    reset_dut();
    eval_csr();
    clear_commit();
    commit_in.pbits.pcsr_uoperation = 1;
    commit_in.pbits.pcsr_uaddress = UINT64_C(0x240);
    commit_in.pbits.pcsr_usource = UINT64_C(0xbad);
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_csr();
    clear_commit();
    csr(0, UINT64_C(0x240), 0, 0);
    eval_csr();
    clear_commit();
    commit_in.pvalid = 1;
    commit_in.pbits.pcsr_uoperation = 1;
    commit_in.pbits.pcsr_uaddress = UINT64_C(0x240);
    commit_in.pbits.pcsr_usource = UINT64_C(0xbad);
    commit_in.pbits.pexception_uvalid = 1;
    commit_in.pbits.pexception_ucause = 2;
    tick_csr();
    clear_commit();
    csr(0, UINT64_C(0x240), 0, 0);
  });
}
