// Sweeps zero-valued HPM and minimal machine CSRs, write intent, XLEN, and S/U permissions.
// SPDX-License-Identifier: Apache-2.0
  typedef logic [XLEN-1:0] word_t;
  typedef struct packed {
    word_t pc;
    logic [31:0] instruction;
    logic [4:0] rd;
    struct packed { logic [1:0] csr; logic immediate; logic [3:0] action; } system;
    struct packed { logic [1:0] action; } fence;
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
  logic interrupt_request, wfi_retired, wfi_wake, writeback_valid;
  word_t writeback_value, mstatus, satp;
  logic [1:0] privilege;
  logic [2:0] frm;
  logic fp_enabled, cbo_zero_enabled, translation_flush;
  logic [1:0] cbo_operation = 0;
  logic [2:0] cbo_permission;

  RV5StageCsrFile dut (.vector_state(), .vector_enabled(), .vector_retire_in('0), .pointer_masking(), .pointer_masking_changed(), .*);
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
    commit_in.bits.system.csr = operation;
    commit_in.bits.system.immediate = immediate;
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

  task automatic writable_zero(input logic [11:0] address);
    access_csr(address, 2, 0, 0);
    for (int operation = 1; operation <= 3; operation++) begin
      access_csr(address, 2'(operation), '1, 1);
      access_csr(address, 2, 0, 0);
      access_csr(address, 2'(operation), word_t'(31), 31, 0, 0, 1, 1);
      access_csr(address, 2, 0, 0);
    end
  endtask

  task automatic readonly_zero(input logic [11:0] address);
    access_csr(address, 2, 0, 0);
    access_csr(address, 3, 0, 0);
    access_csr(address, 2, 0, 0, 0, 0, 1, 1);
    access_csr(address, 3, 0, 0, 0, 0, 1, 1);
    illegal_access(address, 1, 0);
    illegal_access(address, 1, 0, 1, 0);
    // A nonzero rs1 holding zero still expresses CSRRS/CSRRC write intent.
    illegal_access(address, 2, 1);
    illegal_access(address, 3, 1);
    illegal_access(address, 2, 1, 1);
    illegal_access(address, 3, 1, 1);
    access_csr(address, 2, 0, 0);
  endtask

  task automatic enter_mode(input logic [1:0] mode);
    access_csr('h300, 1, word_t'(mode) << 11, 1, 0, 0, 0);
    access_csr('h341, 1, 'h200, 1, 0, 0, 0);
    @(negedge clock);
    commit_in = '0;
    commit_in.valid = 1;
    commit_in.bits.instruction = 32'h30200073;
    commit_in.bits.system.action = 3;
    #1;
    assert (redirect_out.valid && redirect_out.bits == 'h200)
      else $fatal(1, "MRET failed before HPM privilege check");
    @(posedge clock);
    #1;
    commit_in = '0;
    assert (privilege == mode) else $fatal(1, "MRET selected wrong privilege");
  endtask

  initial begin
    commit_in = '0;
    repeat (2) @(posedge clock);
    #1;
    reset = 0;
    assert (privilege == 3) else $fatal(1, "CSR reset privilege mismatch");
    readonly_zero('hf15); // No configuration structure is provided.
    if (XLEN == 32) begin
      writable_zero('h310); // Little-endian, non-hypervisor mstatush.
      writable_zero('h31a); // No implemented high menvcfg fields.
    end else begin
      illegal_access('h310);
      illegal_access('h31a);
    end
    for (int index = 3; index <= 31; index++) begin
      writable_zero(12'('hb00 + index));
      writable_zero(12'('h320 + index));
      readonly_zero(12'('hc00 + index));
      if (XLEN == 32) begin
        writable_zero(12'('hb80 + index));
        readonly_zero(12'('hc80 + index));
      end else begin
        illegal_access(12'('hb80 + index));
        illegal_access(12'('hc80 + index));
      end
    end

    // HPM enable bits remain zero; the existing cycle/time/instret bits survive.
    access_csr('h306, 1, '1, 1);
    access_csr('h106, 1, '1, 1);
    access_csr('h306, 2, 0, 0, 0, 7);
    access_csr('h106, 2, 0, 0, 0, 7);
    for (int mode = 0; mode <= 1; mode++) begin
      enter_mode(2'(mode));
      illegal_access('hf15);
      if (XLEN == 32) begin
        enter_mode(2'(mode));
        illegal_access('h310);
        enter_mode(2'(mode));
        illegal_access('h31a);
      end
      for (int index = 3; index <= 31; index++) begin
        enter_mode(2'(mode));
        illegal_access(12'('hc00 + index));
        if (XLEN == 32) begin
          enter_mode(2'(mode));
          illegal_access(12'('hc80 + index));
        end
        enter_mode(2'(mode));
        illegal_access(12'('hb00 + index));
        enter_mode(2'(mode));
        illegal_access(12'('h320 + index));
      end
    end
    repeat (10) @(negedge clock);
    for (int index = 3; index <= 31; index++)
      access_csr(12'('hc00 + index), 2, 0, 0);
    $display("RV%0d zero-valued Zihpm CSR checks passed", XLEN);
    $finish;
  end

  initial begin
    #100000;
    $fatal(1, "Zihpm CSR test timed out");
  end
