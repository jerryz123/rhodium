// Verifies RV32 timer/state-enable high halves, privilege gates, and supervisor delivery.
// SPDX-License-Identifier: Apache-2.0
module riscv_sstc_rv32_tb;
  struct packed {
    logic valid;
    struct packed {
      logic [31:0] pc, instruction;
      logic [4:0] rd;
      logic [1:0] csr_operation;
    logic [3:0] action;
      logic [11:0] csr_address;
      logic [31:0] csr_source;
      struct packed { logic [31:0] vtype, avl; logic maximum, keep_vl; } vector_config;
      logic exception_valid;
      logic [31:0] exception_cause, exception_value;
    } bits;
  } commit_in;
  struct packed { logic valid; logic [31:0] bits; } redirect_out;
  logic clock = 0, reset = 1;
  logic [5:0] interrupts = 0;
  logic [63:0] time_counter = 64'h12345678ffffffff;
  logic [31:0] hart_id = 0, interrupt_pc = 'h888;
  logic interrupt_boundary = 0;
  logic command_success, writeback_valid, interrupt_request, wfi_wake;
  logic [31:0] writeback_value;
  logic [1:0] privilege;
  RiscvCsrFile dut (.trap_event(), .retire_count({1'b0, command_success}),
    .clock, .reset, .interrupts, .time_counter, .hart_id, .interrupt_pc, .interrupt_boundary,
    .commit_in, .redirect_out, .command_success, .writeback_valid, .writeback_value, .privilege,
    .interrupt_request, .wfi_wake, .fp_update_in('0), .vector_retire_in('0),
    .vector_saturate_in('0), .vector_fault_start_in('0), .vector_truncate_in('0),
    .cbo_operation('0), .wfi(), .mstatus(), .satp(), .frm(), .fp_enabled(),
    .vector_enabled(), .vector_state(), .cbo_zero_access(), .cbo_permission(),
    .translation_flush(), .pbmte(), .pointer_masking(), .pointer_masking_changed()
  );
  always #5 clock = ~clock;

  task automatic csr(input bit write, input logic [11:0] address,
                     input logic [31:0] value, input logic [31:0] expected = 0);
    @(negedge clock); commit_in = '0;
    commit_in.valid = 1; commit_in.bits.rd = 1;
    commit_in.bits.csr_operation = write ? 1 : 2;
    commit_in.bits.csr_address = address; commit_in.bits.csr_source = value;
    #1;
    assert (command_success && writeback_valid && !redirect_out.valid)
      else $fatal(1,"unexpected RV32 timer CSR trap %h",address);
    if (!write) assert (writeback_value == expected)
      else $fatal(1,"CSR %h got %h expected %h",address,writeback_value,expected);
    @(posedge clock); #1; commit_in = '0;
  endtask

  task automatic enter_supervisor;
    csr(1,'h300,'h800); csr(1,'h341,'h800);
    @(negedge clock); commit_in = '0; commit_in.valid = 1; commit_in.bits.action = 3;
    @(posedge clock); #1; commit_in = '0;
    assert (privilege == 1) else $fatal(1,"S entry failed");
  endtask

  task automatic denied(input logic [11:0] address);
    @(negedge clock); commit_in = '0; commit_in.valid = 1;
    commit_in.bits.pc = 'h880; commit_in.bits.rd = 1;
    commit_in.bits.csr_operation = 1; commit_in.bits.csr_address = address;
    commit_in.bits.csr_source = '1;
    #1;
    assert (!command_success && !writeback_valid && redirect_out.valid && redirect_out.bits == 'h1000)
      else $fatal(1,"RV32 state-enable did not deny %h",address);
    @(posedge clock); #1; commit_in = '0;
    csr(0,'h342,0,2); csr(0,'h341,0,'h880);
  endtask

  initial begin
    commit_in = '0;
    repeat (2) @(posedge clock); #1; reset = 0;
    csr(1,'h305,'h1000); csr(1,'h105,'h2000);
    for (int i = 0; i < 4; i++) begin
      csr(0,12'('h31c+i),0,0);
      csr(1,12'('h30c+i),'1); csr(0,12'('h30c+i),0,0);
      csr(1,12'('h31c+i),'1); csr(0,12'('h31c+i),0,i == 0 ? 'hc0000000 : 'h80000000);
      csr(1,12'('h30c+i),0); csr(0,12'('h31c+i),0,i == 0 ? 'hc0000000 : 'h80000000);
      csr(1,12'('h10c+i),'1); csr(0,12'('h10c+i),0,0);
      csr(1,12'('h31c+i),0);
      enter_supervisor(); denied(12'('h10c+i));
      csr(1,12'('h31c+i),'h80000000);
      enter_supervisor(); csr(1,12'('h10c+i),'1); csr(0,12'('h10c+i),0,0);
      denied(12'('h31c+i)); // high halves remain machine-only
    end
    enter_supervisor(); denied('h10a);
    csr(1,'h31c,'hc0000000); enter_supervisor(); csr(1,'h10a,1); csr(0,'h10a,0,1);
    denied('h30c); csr(0,'h10a,0,1);
    csr(1,'h14d,'hffffffff); csr(1,'h15d,'h12345679);
    csr(0,'h14d,0,'hffffffff); csr(0,'h15d,0,'h12345679);
    csr(1,'h31a,'h80000000); csr(0,'h31a,0,'h80000000);
    csr(1,'h30a,1); csr(0,'h30a,0,1); csr(0,'h31a,0,'h80000000);
    csr(0,'h344,0,0);
    csr(1,'h14d,0); csr(0,'h15d,0,'h12345679); csr(0,'h344,0,0);
    time_counter = 64'h1234567900000000; csr(0,'h344,0,'h20);
    csr(1,'h15d,'hffffffff); csr(0,'h14d,0,0); csr(0,'h344,0,0);
    // The sign bit must not turn a future timer into an expired signed value.
    time_counter = 64'h8000000000000000; csr(0,'h344,0,0);
    csr(1,'h15d,'h80000000); csr(0,'h344,0,'h20);
    csr(1,'h14d,1); csr(0,'h344,0,0);
    // STIP writes cannot change the enabled comparator; disabling restores injection.
    csr(1,'h344,'h20); csr(0,'h344,0,0);
    csr(1,'h31a,0); csr(1,'h344,'h20); csr(0,'h344,0,'h20);
    csr(1,'h344,0); csr(1,'h31a,'h80000000);
    csr(1,'h306,2); csr(1,'h303,'h20); csr(1,'h304,'h20);
    csr(1,'h300,'h802); csr(1,'h341,'h800);
    @(negedge clock); commit_in = '0; commit_in.valid = 1; commit_in.bits.action = 3;
    @(posedge clock); #1; commit_in = '0;
    assert (privilege == 1) else $fatal(1,"MRET failed");
    csr(0,'h15d,0,'h80000000); csr(1,'h14d,2);
    time_counter = 64'h8000000000000002; #1;
    assert (interrupt_request && wfi_wake) else $fatal(1,"RV32 Sstc not delivered");
    @(negedge clock); interrupt_boundary = 1; #1;
    assert (redirect_out.valid && redirect_out.bits == 'h2000) else $fatal(1,"RV32 timer target");
    @(posedge clock); #1; interrupt_boundary = 0;
    csr(0,'h142,0,'h80000005); csr(0,'h141,0,'h888);
    csr(1,'h14d,3); csr(0,'h144,0,0);
    $display("rv5stage RV32 Sstc PASS"); $finish;
  end
  initial begin #10000; $fatal(1,"RV32 Sstc timeout"); end
endmodule
