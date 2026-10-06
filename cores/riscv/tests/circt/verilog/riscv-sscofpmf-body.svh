// Checks HPM accounting, writable S/H counter enables, permissions, overflow, and interrupts.
// SPDX-License-Identifier: Apache-2.0
  typedef logic [XLEN-1:0] word_t;
  typedef struct packed {
    word_t pc;
    logic [31:0] instruction;
    logic [4:0] rd;
    logic [1:0] csr_operation;
    logic [3:0] action;
    logic [11:0] csr_address;
    word_t csr_source;
    struct packed { word_t vtype; word_t avl; logic maximum; logic keep_vl; } vector_config;
    logic exception_valid;
    word_t exception_cause;
    word_t exception_value;
  } commit_bits_t;
  logic clock = 0;
  logic reset = 1;
  logic [5:0] interrupts = 0;
  word_t hart_id = 0;
  logic [63:0] time_counter = 0;
  logic interrupt_boundary = 0;
  word_t interrupt_pc = 0;
  struct packed { logic valid; commit_bits_t bits; } commit_in;
  logic [5:0] fp_update_in = 0;
  struct packed { logic valid; word_t bits; } redirect_out;
  logic command_success;
  logic interrupt_request, wfi, wfi_wake, writeback_valid;
  word_t writeback_value, mstatus, satp;
  logic [1:0] privilege;
  logic [2:0] frm;
  logic fp_enabled, translation_flush;
  logic [1:0] cbo_zero_access;
  logic [1:0] cbo_operation = 0;
  logic [3:0] cbo_permission;

  logic virtualized;
`ifdef RHODIUM_HPM_TEST_H
    struct packed { logic [1:0] privilege; logic virtualized; } execution_context;
    assign virtualized = execution_context.virtualized;
    RiscvCsrFile dut (.retire(command_success), .guest_fault_in('0), .execution_context(execution_context),
      .guest_translation(), .guest_pointer_masking(), .pbmte(), .vector_state(),
      .vector_enabled(), .vector_retire_in('0), .vector_saturate_in('0),
      .vector_fault_start_in('0), .vector_truncate_in('0), .pointer_masking(),
      .pointer_masking_changed(), .*);
`else
    assign virtualized = 0;
  RiscvCsrFile dut (.retire(command_success), .pbmte(), .vector_state(), .vector_enabled(), .vector_retire_in('0), .vector_saturate_in('0), .vector_fault_start_in('0), .vector_truncate_in('0), .pointer_masking(), .pointer_masking_changed(), .*);
`endif
  always #5 clock = ~clock;

  task automatic access_csr(
    input logic [11:0] address,
    input logic [1:0] operation,
    input word_t source,
    input logic [4:0] source_specifier,
    input bit expect_trap = 0,
    input word_t expected_old = 0,
    input bit check_old = 1,
    input bit immediate = 0,
    input logic [4:0] rd = 1
  );
    logic [31:0] instruction;
    instruction = {address, source_specifier, immediate, operation, rd, 7'h73};
    @(negedge clock);
    commit_in = '0;
    commit_in.valid = 1;
    commit_in.bits.pc = 'h100;
    commit_in.bits.instruction = instruction;
    commit_in.bits.rd = rd;
    commit_in.bits.csr_operation = operation;
    commit_in.bits.csr_address = address;
    commit_in.bits.csr_source = source;
    #1;
    assert (redirect_out.valid == expect_trap && !translation_flush)
      else $fatal(1, "RV%0d CSR %03h op %0d privilege %0d trap mismatch", XLEN, address, operation, privilege);
    if (expect_trap) begin
      assert (!writeback_valid && redirect_out.bits == 0)
        else $fatal(1, "illegal HPM access wrote a register or selected wrong trap vector");
    end else begin
      assert (writeback_valid && (!check_old || writeback_value == expected_old))
        else $fatal(1, "CSR %03h returned %h instead of %h", address, writeback_value, expected_old);
    end
    @(posedge clock);
    #1;
    commit_in = '0;
    if (expect_trap)
      assert (privilege == 3) else $fatal(1, "HPM exception did not enter M mode");
  endtask

  task automatic illegal_access(
    input logic [11:0] address,
    input logic [1:0] operation = 2,
    input logic [4:0] source_specifier = 0,
    input bit immediate = 0,
    input logic [4:0] rd = 1
  );
    logic [31:0] instruction;
    instruction = {address, source_specifier, immediate, operation, rd, 7'h73};
    access_csr(address, operation, 0, source_specifier, 1, 0, 1, immediate, rd);
    access_csr('h342, 2, 0, 0, 0, 2);
    access_csr('h343, 2, 0, 0, 0, word_t'(instruction));
    access_csr('h341, 2, 0, 0, 0, 'h100);
  endtask

  task automatic enter_mode(input logic [1:0] mode);
    access_csr('h300, 1, word_t'(mode) << 11, 1, 0, 0, 0);
    access_csr('h341, 1, 'h200, 1, 0, 0, 0);
    @(negedge clock);
    commit_in = '0;
    commit_in.valid = 1;
    commit_in.bits.instruction = 32'h30200073;
    commit_in.bits.action = 3;
    #1;
    assert (redirect_out.valid && redirect_out.bits == 'h200)
      else $fatal(1, "MRET failed before HPM privilege check");
    @(posedge clock);
    #1;
    commit_in = '0;
    assert (privilege == mode) else $fatal(1, "MRET selected wrong privilege");
  endtask


  task automatic wr(input logic [11:0] address, input word_t value);
    access_csr(address, 1, value, 1, 0, 0, 0);
  endtask
  task automatic rd(input logic [11:0] address, input word_t value);
    access_csr(address, 2, 0, 0, 0, value);
  endtask
  task automatic wr64(input logic [11:0] address, input logic [63:0] value);
    wr(address, word_t'(value));
    if (XLEN == 32) wr(address == 'h323 ? 12'h723 : 12'hb83, word_t'(value >> 32));
  endtask
  task automatic freeze;
    wr('h320, 8);
  endtask
  task automatic reset_state;
    @(negedge clock); commit_in = '0; interrupt_boundary = 0; interrupts = 0; reset = 1;
    repeat (2) @(posedge clock);
    @(negedge clock); reset = 0;
  endtask
  task automatic retire_nop;
    @(negedge clock); commit_in = '0; commit_in.valid = 1;
    commit_in.bits.instruction = 'h13;
    @(posedge clock); #1; commit_in = '0;
  endtask
  task automatic enter_guest(input logic [1:0] mode);
    wr('h300, (word_t'(1) << 39) | (word_t'(mode) << 11));
    wr('h341, 'h200);
    @(negedge clock); commit_in = '0; commit_in.valid = 1;
    commit_in.bits.action = 3; commit_in.bits.instruction = 'h30200073;
    @(posedge clock); #1; commit_in = '0;
    assert (virtualized && privilege == mode) else $fatal(1, "guest entry");
  endtask

  initial begin
    reset_state();
    freeze();
    rd('h320, 8);
    wr('h320, '1); rd('h320, 8);
    wr64('hb03, 64'h123456789abcdef0);
    rd('hb03, word_t'(64'h123456789abcdef0));
    rd('hc03, word_t'(64'h123456789abcdef0));
    if (XLEN == 32) begin rd('hb83, 'h12345678); rd('hc83, 'h12345678); end
    // Unimplemented slots still ignore writes, including their filter halves.
    for (int i = 4; i < 32; ++i) begin
      wr(12'hb00 + 12'(i), '1); rd(12'hb00 + 12'(i), 0);
      wr(12'h320 + 12'(i), '1); rd(12'h320 + 12'(i), 0);
      if (XLEN == 32) begin wr(12'h720 + 12'(i), '1); rd(12'h720 + 12'(i), 0); end
    end
    wr64('h323, 64'hfc00000000000000);
    rd('h323, XLEN == 64 ? word_t'(HYPERVISOR ? 64'hfc00000000000000 : 64'hf000000000000000) : 0);
    if (XLEN == 32) rd('h723, word_t'('hf0000000));
    else illegal_access('h723);
    rd('hda0, 8);
    wr64('h323, 64'hffffffffffffffff);
    rd('h323, XLEN == 64 ? word_t'(HYPERVISOR ? 64'hfc00000000000000 : 64'hf000000000000000) : 0);
    // OF does not directly drive pending, and scountovf is read only.
    rd('h344, 0);
    illegal_access('hda0, 1, 1);
    // Sscounterenw/Shcounterenw: independently writable enables, including
    // counter 3 with real nonzero storage. Zero slots need no writable enable.
    for (int index = 0; index < 32; ++index) begin
      wr('h106, word_t'(1) << index);
      rd('h106, index < 4 ? word_t'(1) << index : 0);
      wr('h106, 0); rd('h106, 0);
      if (HYPERVISOR) begin
        wr('h606, word_t'(1) << index);
        rd('h606, index < 4 ? word_t'(1) << index : 0);
        wr('h606, 0); rd('h606, 0);
      end
    end
    wr('h306, '1); rd('h306, 15);
    wr('h106, '1); rd('h106, 15);
    enter_mode(1);
    wr('h106, 0); rd('h106, 0);
    wr('h106, 8); rd('h106, 8);
    rd('hda0, 8); rd('hc03, word_t'(64'h123456789abcdef0));
    illegal_access('hc04);
    wr('h306, 0);
    enter_mode(1); rd('hda0, 0); illegal_access('hc03);
    wr('h306, 8); wr('h106, 0);
    enter_mode(0); illegal_access('hc03);
    wr('h106, 8); enter_mode(0); rd('hc03, word_t'(64'h123456789abcdef0)); illegal_access('hda0);

    // Retirement uses accepted WB instructions; bubbles and synchronous faults do not count.
    reset_state(); wr64('h323, 2); wr64('hb03, 0);
    repeat (4) @(posedge clock);
    retire_nop(); retire_nop();
    @(negedge clock); commit_in = '0; commit_in.valid = 1;
    commit_in.bits.exception_valid = 1; commit_in.bits.exception_cause = 2;
    @(posedge clock); #1; commit_in = '0;
    freeze(); rd('hb03, 3); // Two NOPs and the mcountinhibit write itself.
    // Filtering in M suppresses even valid retirements.
    wr64('h323, 64'h4000000000000002); wr64('hb03, 0);
    wr('h320, 0); retire_nop(); freeze(); rd('hb03, 0);

    // The integrated event payload selects S/U/VS/VU filters from live context.
    for (int mode = 0; mode < (HYPERVISOR ? 4 : 2); ++mode) begin
      for (int filtered = 0; filtered < 2; ++filtered) begin
        reset_state();
        wr64('h323, 64'h4000000000000002 |
             (filtered != 0 ? (64'b1 << (mode == 0 ? 60 : mode == 1 ? 61 : mode == 2 ? 59 : 58)) : 0));
        wr64('hb03, 0);
        if (mode < 2) enter_mode(2'(mode));
        else enter_guest(mode == 2 ? 2'd1 : 2'd0);
        retire_nop(); retire_nop();
        @(negedge clock); commit_in = '0; commit_in.valid = 1;
        commit_in.bits.exception_valid = 1; commit_in.bits.exception_cause = 2;
        @(posedge clock); #1; commit_in = '0;
        freeze(); rd('hb03, filtered != 0 ? 0 : 2);
      end
    end

    reset_state(); wr64('h323, 1); wr64('hb03, 0);
    repeat (5) @(posedge clock);
    freeze();
    @(negedge clock); commit_in = '0; commit_in.valid = 1;
    commit_in.bits.csr_operation = 2; commit_in.bits.csr_address = 'hb03; commit_in.bits.rd = 1;
    #1; assert (writeback_valid && writeback_value >= 5) else $fatal(1, "cycle event read valid=%b value=%h", writeback_valid, writeback_value);
    @(posedge clock); #1; commit_in = '0;

    // A retiring CSR clearing pending on the wrap cycle cannot lose overflow.
    reset_state(); wr64('h323, 2); wr64('hb03, '1);
    wr('h344, 0); freeze();
    rd('hda0, 8); rd('h344, 'h2000);
    wr('h344, 0); rd('h344, 0); rd('hda0, 8);
    wr64('hb03, '1); wr('h320, 0); retire_nop(); freeze();
    rd('h344, 0); // OF still set, so a second wrap cannot re-pend.
    wr64('h323, 2); wr64('hb03, '1); wr('h320, 0); retire_nop(); freeze();
    rd('h344, 'h2000);
    // Local enable wakes WFI even while global MIE masks delivery.
    wr('h304, 'h2000);
    assert (wfi_wake && !interrupt_request) else $fatal(1, "masked overflow wake");
    wr('h300, 8);
    assert (interrupt_request) else $fatal(1, "overflow interrupt not requested");
    @(negedge clock); interrupt_pc = 'h444; interrupt_boundary = 1; #1;
    assert (redirect_out.valid) else $fatal(1, "overflow boundary missing");
    @(posedge clock); #1; interrupt_boundary = 0;
    rd('h342, (word_t'(1) << (XLEN-1)) | 13); rd('h341, 'h444);
    rd('h344, 'h2000); // Trap entry does not clear pending.
    wr('h344, 0);

    // Delegated overflow reaches S, and sip can clear just its delegated bit.
    wr('h303, 'h2000); wr('h344, 'h2000); enter_mode(0);
    @(negedge clock); interrupt_pc = 'h448; interrupt_boundary = 1;
    @(posedge clock); #1; interrupt_boundary = 0;
    assert (privilege == 1) else $fatal(1, "overflow was not delegated");
    rd('h142, (word_t'(1) << (XLEN-1)) | 13);
    rd('h141, 'h448); rd('h144, 'h2000); wr('h144, 0); rd('h144, 0);

    if (HYPERVISOR) begin
      reset_state(); freeze(); wr64('h323, 64'h8000000000000000);
      wr('h306, 8); wr('h606, 0); enter_guest(1); rd('hda0, 0);
      access_csr('hc03, 2, 0, 0, 1);
      rd('h342, 22); // Missing hcounteren raises virtual instruction.
      wr('h606, 8); enter_guest(1); rd('hda0, 8); rd('hc03, 0);
      // Return via an explicit illegal machine CSR, then test host routing.
      illegal_access('h300);
      wr('h303, 'h2000); wr('h603, 'h2000); rd('h603, 0);
      wr('h304, 'h2000); wr('h344, 'h2000); enter_guest(1);
      @(negedge clock); interrupt_pc = 'h450; interrupt_boundary = 1;
      @(posedge clock); #1; interrupt_boundary = 0;
      assert (!virtualized && privilege == 1) else $fatal(1, "LCOFI did not exit guest to HS");
      rd('h142, (word_t'(1) << 63) | 13);

      // An HS-targeted virtual interrupt precedes HS LCOFI. Neither is sent
      // directly to VS, whose normal virtual causes have lower privilege.
      reset_state(); wr('h303, 'h2000); wr('h304, 'h2040);
      wr('h344, 'h2000); wr('h645, 'h40); enter_guest(1);
      @(negedge clock); interrupt_pc = 'h454; interrupt_boundary = 1;
      @(posedge clock); #1; interrupt_boundary = 0;
      assert (!virtualized && privilege == 1) else $fatal(1, "HS interrupt priority target");
      rd('h142, (word_t'(1) << 63) | 6);

      reset_state(); wr('h303, 'h2000); wr('h603, 'h40); wr('h304, 'h2040);
      wr('h200, 2); wr('h344, 'h2000); wr('h645, 'h40); enter_guest(1);
      @(negedge clock); interrupt_pc = 'h458; interrupt_boundary = 1;
      @(posedge clock); #1; interrupt_boundary = 0;
      assert (!virtualized && privilege == 1) else $fatal(1, "HS LCOFI must outrank VS interrupt");
      rd('h142, (word_t'(1) << 63) | 13);
    end
    $display("PASS integrated Sscofpmf RV%0d H=%0d", XLEN, HYPERVISOR);
    $finish;
  end
