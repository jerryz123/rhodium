// Exercises guest CSR state, state-enable and timer gates, interrupt priority, traps, and returns.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_hypervisor_csr_tb;
  typedef struct packed {
    logic [63:0] pc;
    logic [31:0] instruction;
    logic [4:0] rd;
    struct packed { logic [1:0] csr; logic immediate; logic [3:0] action; } system;
    struct packed { logic [2:0] action; } fence;
    logic [11:0] csr_address;
    logic [63:0] csr_source;
    struct packed { logic [63:0] vtype; logic [63:0] avl; logic maximum; logic keep_vl; } vector_config;
    logic exception_valid;
    logic [63:0] exception_cause;
    logic [63:0] exception_value;
  } commit_bits_t;
  struct packed { logic valid; commit_bits_t bits; } commit_in;
  struct packed { logic valid; logic [63:0] bits; } redirect_out;
  struct packed {
    logic valid;
    struct packed { logic guest_virtual_address; logic [63:0] guest_physical_address; logic access; } bits;
  } guest_fault_in;
  struct packed { logic [1:0] privilege; logic virtualized; } execution_context;
  logic clock = 0;
  logic reset = 1;
  logic [5:0] interrupts = 0;
  logic [63:0] time_counter = 100;
  logic [63:0] hart_id = 0;
  logic interrupt_boundary = 0;
  logic [63:0] interrupt_pc = 0;
  logic [5:0] fp_update_in = 0;
  logic [1:0] cbo_operation = 0;
  logic [3:0] cbo_permission;
  logic [1:0] cbo_zero_access;
  logic interrupt_request, retired, wfi_retired, wfi_wake, writeback_valid;
  logic [63:0] writeback_value, mstatus, satp;
  logic [1:0] privilege;
  logic [2:0] frm, pointer_masking, guest_pointer_masking;
  logic fp_enabled, vector_enabled, translation_flush, pbmte, pointer_masking_changed;
  struct packed {logic vs_pbmte, virtualized; logic [63:0] hstatus, vsstatus, vsatp, hgatp;} guest_translation;
  RV5StageCsrFile dut (.vector_state(), .vector_retire_in('0),
    .vector_saturate_in('0), .vector_fault_start_in('0), .vector_truncate_in('0), .*);
  always #5 clock = ~clock;

  localparam logic [63:0] MPV = 64'h8000000000;
  localparam logic [63:0] GVA = 64'h4000000000;

  task automatic clear_commit;
    commit_in = '0;
    guest_fault_in = '0;
    interrupt_boundary = 0;
  endtask

  task automatic check_context(input logic [1:0] mode, input logic virtualized);
    assert (privilege == mode && execution_context.privilege == mode &&
            execution_context.virtualized == virtualized)
      else $fatal(1, "wrong execution context: %b, expected %b/%b", execution_context, mode, virtualized);
  endtask

  task automatic csr(input logic write, input logic [11:0] address,
                     input logic [63:0] value, input logic [63:0] expected = 0,
                     input int expected_flush = -1, input int expected_mask_change = -1);
    @(negedge clock);
    clear_commit();
    commit_in.valid = 1;
    commit_in.bits.rd = 1;
    commit_in.bits.system.csr = write ? 1 : 2;
    commit_in.bits.csr_address = address;
    commit_in.bits.csr_source = value;
    #1;
    if (expected_flush >= 0)
      assert (translation_flush == 1'(expected_flush)) else $fatal(1,"PBMTE flush mismatch CSR=%h",address);
    if (expected_mask_change >= 0)
      assert (pointer_masking_changed == 1'(expected_mask_change)) else $fatal(1,"PMM restart mismatch CSR=%h",address);
    assert (retired && writeback_valid && !redirect_out.valid)
      else $fatal(1, "unexpected trap accessing CSR %h", address);
    if (!write)
      assert (writeback_value == expected)
        else $fatal(1, "CSR %h: got %h expected %h", address, writeback_value, expected);
    @(posedge clock); #1; clear_commit();
  endtask

  task automatic command(input logic [3:0] action, input logic [63:0] target,
                          input logic traps = 0, input logic [63:0] pc = 'h804);
    @(negedge clock);
    clear_commit();
    commit_in.valid = 1;
    commit_in.bits.system.action = action;
    commit_in.bits.pc = pc;
    commit_in.bits.instruction = action == 4 ? 32'h10200073 : 32'h00000073;
    #1;
    assert (redirect_out.valid && redirect_out.bits == target && retired == !traps)
      else $fatal(1, "wrong command redirect: action %d target %h expected %h", action, redirect_out.bits, target);
    @(posedge clock); #1; clear_commit();
  endtask

  task automatic reset_dut(input bit enable_state = 1);
    @(negedge clock); reset = 1; clear_commit(); interrupts = 0; fp_update_in = 0;
    repeat (2) @(posedge clock);
    #1; reset = 0; check_context(3, 0);
    csr(1, 'h305, 'h1000);
    csr(1, 'h105, 'h2000);
    csr(1, 'h205, 'h3000);
    csr(1, 'h302, 'h00ffffff);
    if (enable_state) begin
      for (int i = 0; i < 4; i++) csr(1,12'('h30c+i),'1);
      for (int i = 0; i < 4; i++) csr(1,12'('h60c+i),'1);
    end
  endtask

  task automatic enter_guest(input logic user_mode = 0, input logic [1:0] host_fs = 0, input logic [1:0] host_vs = 0);
    csr(1, 'h300, MPV | (user_mode ? 64'h0 : 64'h800) | (64'(host_fs) << 13) | (64'(host_vs) << 9));
    csr(1, 'h341, 'h800);
    command(3, 'h800);
    check_context(user_mode ? 0 : 1, 1);
    assert ((mstatus & MPV) == 0) else $fatal(1, "MRET did not clear MPV");
  endtask

  task automatic guest_fault(input logic [63:0] cause, input logic implicit_pte,
                             input logic to_machine, input logic [63:0] fault_address = 'h123456780);
    @(negedge clock); clear_commit();
    commit_in.valid = 1;
    commit_in.bits.pc = 'h880;
    commit_in.bits.exception_valid = 1;
    commit_in.bits.exception_cause = cause;
    commit_in.bits.exception_value = 'h12345678;
    guest_fault_in.valid = 1;
    guest_fault_in.bits.guest_virtual_address = 1;
    guest_fault_in.bits.guest_physical_address = fault_address;
    guest_fault_in.bits.access = implicit_pte;
    #1;
    assert (!retired && redirect_out.valid && redirect_out.bits == (to_machine ? 'h1000 : 'h2000))
      else $fatal(1, "guest fault went to wrong handler");
    @(posedge clock); #1; clear_commit();
    check_context(to_machine ? 3 : 1, 0);
    csr(0, to_machine ? 12'h342 : 12'h142, 0, cause);
    csr(0, to_machine ? 12'h343 : 12'h143, 0, 'h12345678);
    csr(0, to_machine ? 12'h34b : 12'h643, 0, fault_address >> 2);
    csr(0, to_machine ? 12'h34a : 12'h64a, 0, implicit_pte ? 'h3000 : 0);
  endtask

  task automatic denied_csr(input logic [11:0] address, input logic [63:0] cause,
                            input logic write = 0);
    @(negedge clock); clear_commit();
    commit_in.valid = 1; commit_in.bits.pc = 'h884; commit_in.bits.rd = 1;
    commit_in.bits.system.csr = write ? 1 : 2;
    commit_in.bits.csr_address = address; commit_in.bits.csr_source = 'hffff;
    commit_in.bits.instruction = {address, 20'h020f3};
    #1;
    assert (!retired && !writeback_valid && !translation_flush &&
            redirect_out.valid && redirect_out.bits == 'h2000)
      else $fatal(1, "CSR %h was not rejected", address);
    @(posedge clock); #1; clear_commit(); check_context(1, 0);
    csr(0, 'h142, 0, cause);
    csr(0, 'h143, 0, {32'h0, address, 20'h020f3});
  endtask

  task automatic fence(input logic [2:0] action, input bit legal, input int cause = 0);
    @(negedge clock); clear_commit();
    commit_in.valid = 1;
    commit_in.bits.pc = 'h888;
    commit_in.bits.fence.action = action;
    commit_in.bits.instruction = action == 4 ? 32'h22000073 : 32'h62000073;
    #1;
    assert (retired == legal && translation_flush == legal && redirect_out.valid == !legal)
      else $fatal(1, "HFENCE legality/flush mismatch");
    @(posedge clock); #1; clear_commit();
    if (!legal) csr(0, 'h142, 0, 64'(cause));
  endtask

  task automatic take_interrupt(input logic [63:0] target, input logic [1:0] mode,
                                input bit guest, input logic [63:0] cause);
    @(negedge clock); clear_commit(); interrupt_pc = 'habc; interrupt_boundary = 1;
    #1;
    assert (interrupt_request && redirect_out.valid && redirect_out.bits == target)
      else $fatal(1, "interrupt target %h expected %h", redirect_out.bits, target);
    @(posedge clock); #1; clear_commit(); check_context(mode, guest);
    csr(0, mode == 3 ? 12'h342 : 12'h142, 0, 64'h8000000000000000 | cause);
    csr(0, mode == 3 ? 12'h341 : 12'h141, 0, 'habc);
    csr(0, mode == 3 ? 12'h343 : 12'h143, 0, 0);
  endtask

  task automatic virtual_interrupts;
    // One enable store and one injected-pending store back all M/H/VS views.
    reset_dut(); csr(0, 'h303, 0, 'h444);
    csr(1, 'h303, 'hffff); csr(0, 'h303, 0, 'h666); csr(1, 'h303, 0);
    csr(1, 'h604, 'hffff); csr(0, 'h304, 0, 'h444);
    csr(0, 'h104, 0, 0); csr(1, 'h104, 'hffff); csr(0, 'h604, 0, 'h444);
    csr(1, 'h645, 'hffff); csr(0, 'h644, 0, 'h444); csr(0, 'h344, 0, 'h444);
    csr(0, 'h144, 0, 0); csr(0, 'h244, 0, 0); csr(0, 'h204, 0, 0);
    csr(1, 'h204, 0); csr(0, 'h604, 0, 'h444);
    csr(1, 'h603, 'hffff); csr(0, 'h603, 0, 'h444);
    csr(0, 'h244, 0, 'h222); csr(0, 'h204, 0, 'h222);
    csr(1, 'h644, 0); csr(0, 'h645, 0, 'h440);
    csr(1, 'h344, 4); csr(0, 'h645, 0, 'h444);
    csr(1, 'h244, 0); csr(0, 'h645, 0, 'h440);
    csr(1, 'h244, 'hffff); csr(0, 'h645, 0, 'h444);
    csr(1, 'h304, 'heee); csr(1, 'h204, 0); csr(0, 'h304, 0, 'haaa);
    csr(1, 'h204, 'hffff); csr(0, 'h304, 0, 'heee);
    csr(1, 'h607, 'hffff); csr(0, 'h607, 0, 0); csr(0, 'he12, 0, 0);

    for (int index = 0; index < 3; index++) begin
      reset_dut(); csr(1, 'h603, 'h444);
      csr(1, 'h604, 64'd1 << (2+index*4)); csr(1, 'h645, 64'd1 << (2+index*4));
      csr(1, 'h200, 2); enter_guest(); #1;
      assert (interrupt_request && wfi_wake && !redirect_out.valid)
        else $fatal(1, "missing deferred VS interrupt: request=%b wake=%b redirect=%b", interrupt_request, wfi_wake, redirect_out.valid);
      take_interrupt('h3000, 1, 1, 64'(1+index*4));
      csr(0, 'h100, 0, 'h200000120);
      csr(0, 'h144, 0, 64'd1 << (1+index*4));
      csr(1, 'h144, 0); // only injected software pending is guest-writable
      csr(0, 'h144, 0, index == 0 ? 0 : 64'd1 << (1+index*4));
      csr(1, 'h104, 0); command(4, 'habc); check_context(1, 1); #1;
      assert (!interrupt_request) else $fatal(1, "VS enable alias did not mask delivery");
      command(1, 'h2000, 1); csr(1, 'h645, 0); csr(0, 'h644, 0, 0);
    end

    // Global masking suppresses delivery, not WFI wake; VU ignores VS SIE.
    reset_dut(); csr(1, 'h603, 4); csr(1, 'h604, 4); csr(1, 'h645, 4); enter_guest();
    #1; assert (!interrupt_request && wfi_wake) else $fatal(1, "VS global mask/WFI");
    csr(1, 'h100, 2); take_interrupt('h3000, 1, 1, 1);
    reset_dut(); csr(1, 'h603, 4); csr(1, 'h604, 4); csr(1, 'h645, 4); enter_guest(1);
    take_interrupt('h3000, 1, 1, 1); csr(0, 'h100, 0, 'h200000000);
    csr(1, 'h144, 0); command(4, 'habc); check_context(0, 1);

    // Delegated virtual interrupts cannot preempt V=0. Undelegated ones target
    // HS with original cause numbers, above any delegated VS interrupt.
    reset_dut(); csr(1, 'h603, 'h444); csr(1, 'h604, 'h444); csr(1, 'h645, 'h444);
    csr(1, 'h300, 'h802); csr(1, 'h341, 'h800); command(3, 'h800); #1;
    assert (!interrupt_request) else $fatal(1, "VS interrupt escaped V=0");
    csr(1, 'h603, 'h404); take_interrupt('h2000, 1, 0, 6);
    reset_dut(); csr(1, 'h603, 'h404); csr(1, 'h604, 'h444); csr(1, 'h645, 'h444);
    csr(1, 'h200, 2); enter_guest(); take_interrupt('h2000, 1, 0, 6);
    reset_dut(); csr(1, 'h604, 'h444); csr(1, 'h645, 'h444); enter_guest();
    take_interrupt('h2000, 1, 0, 10);
    csr(1, 'h645, 'h44); command(4, 'habc); take_interrupt('h2000, 1, 0, 2);
    csr(1, 'h645, 'h40); command(4, 'habc); take_interrupt('h2000, 1, 0, 6);

    // Physical HS beats VS; machine delivery beats both. No pending source is
    // consumed by entry, so masks and software acknowledgement own progress.
    reset_dut(); csr(1, 'h603, 'h444); csr(1, 'h604, 'h444); csr(1, 'h645, 'h444);
    csr(1, 'h303, 'h20); csr(1, 'h304, 'h464); csr(1, 'h200, 2); enter_guest();
    interrupts = 6'b001000; take_interrupt('h2000, 1, 0, 5); interrupts = 0;
    csr(1, 'h645, 'h444); command(4, 'habc); take_interrupt('h3000, 1, 1, 9);
    reset_dut(); csr(1, 'h603, 'h444); csr(1, 'h604, 'h444); csr(1, 'h645, 'h444);
    csr(1, 'h304, 'h4c4); csr(1, 'h200, 2); enter_guest();
    interrupts = 6'b000100; take_interrupt('h1000, 3, 0, 7); interrupts = 0;

    // Synchronous exceptions still win at a coincident interrupt boundary.
    reset_dut(); csr(1, 'h603, 4); csr(1, 'h604, 4); csr(1, 'h645, 4); csr(1, 'h200, 2); enter_guest();
    @(negedge clock); clear_commit(); interrupt_boundary = 1; interrupt_pc = 'hccc;
    commit_in.valid = 1; commit_in.bits.pc = 'h888;
    commit_in.bits.exception_valid = 1; commit_in.bits.exception_cause = 2;
    #1; assert (!retired && redirect_out.bits == 'h2000) else $fatal(1, "interrupt beat exception");
    @(posedge clock); #1; clear_commit(); csr(0, 'h142, 0, 2); csr(0, 'h141, 0, 'h888);
    csr(0, 'h645, 0, 4);
  endtask

  task automatic supervisor_timers;
    // Comparator reads/writes in M are legal before STCE/counter delegation.
    reset_dut(); time_counter = 100;
    csr(1,'h14d,101); csr(1,'h24d,151);
    csr(1,'h605,50); csr(1,'h60a,64'h8000000000000000);
    csr(0,'h60a,0,0); // H.STCE is read-only zero while M.STCE=0.
    csr(1,'h344,'h20); csr(0,'h344,0,'h20);
    csr(1,'h30a,64'h8000000000000000);
    csr(1,'h60a,64'h8000000000000000);
    csr(0,'h344,0,0); csr(0,'h644,0,0);
    // Legacy physical/software STIP cannot override an enabled comparator.
    interrupts = 6'b001000; csr(1,'h344,'h20); csr(0,'h344,0,0);
    time_counter = 101; csr(0,'h344,0,'h60); csr(0,'h644,0,'h40);
    csr(1,'h14d,102); csr(1,'h24d,152); csr(0,'h344,0,0);
    csr(1,'h645,'h40); csr(0,'h644,0,'h40);
    csr(1,'h645,0); csr(0,'h644,0,0);
    csr(1,'h60a,0); time_counter = 102; csr(0,'h344,0,'h20);
    csr(1,'h30a,0); csr(0,'h344,0,'h20);
    interrupts = 0; csr(1,'h344,0); csr(0,'h344,0,0);

    // Unsigned comparisons and offset overflow use all 64 bits, even at V=0.
    csr(1,'h30a,64'h8000000000000000); csr(1,'h60a,64'h8000000000000000);
    time_counter = 64'hfffffffffffffffe; csr(1,'h605,3);
    csr(1,'h14d,64'hffffffffffffffff); csr(1,'h24d,2); csr(0,'h344,0,0);
    time_counter = 64'hffffffffffffffff; csr(0,'h344,0,'h60);
    time_counter = 0; csr(1,'h605,0); csr(0,'h344,0,0);

    // Exercise every machine/guest STCE and TM combination, read and write.
    for (int gates = 0; gates < 16; gates++) begin
      for (int wr = 0; wr < 2; wr++) begin
        reset_dut(); time_counter = 100;
        csr(1,'h14d,200); csr(1,'h24d,300);
        csr(1,'h30a,gates[0] ? 64'h8000000000000000 : 0);
        csr(1,'h306,gates[1] ? 2 : 0);
        csr(1,'h60a,gates[2] ? 64'h8000000000000000 : 0);
        csr(1,'h606,gates[3] ? 2 : 0);
        enter_guest();
        if (!gates[0] || !gates[1]) denied_csr('h14d,2,1'(wr));
        else if (!gates[2] || !gates[3]) denied_csr('h14d,22,1'(wr));
        else begin
          csr(1'(wr),'h14d,400,300);
          command(1,'h2000,1);
          csr(0,'h14d,0,200); csr(0,'h24d,0,wr != 0 ? 400 : 300);
        end
      end
    end
    // HS can access either timer without H.STCE/H.TM, but not without M.TM.
    reset_dut(); csr(1,'h30a,64'h8000000000000000); csr(1,'h306,2);
    csr(1,'h300,'h800); csr(1,'h341,'h800); command(3,'h800);
    csr(1,'h14d,200); csr(1,'h24d,300);
    reset_dut(); csr(1,'h30a,64'h8000000000000000);
    csr(1,'h300,'h800); csr(1,'h341,'h800); command(3,'h800); denied_csr('h24d,2);
    // VU cannot use supervisor aliases, nor can VS name the HS bank directly.
    reset_dut(); csr(1,'h30a,64'h8000000000000000); csr(1,'h306,2);
    csr(1,'h60a,64'h8000000000000000); csr(1,'h606,2); enter_guest(1); denied_csr('h14d,22);
    reset_dut(); csr(1,'h30a,64'h8000000000000000); csr(1,'h306,2); enter_guest(); denied_csr('h24d,22);

    // A guest timer wakes WFI with SIE clear, then vectors only when enabled.
    reset_dut(); time_counter = 100;
    csr(1,'h14d,1000); csr(1,'h24d,151); csr(1,'h605,50);
    csr(1,'h30a,64'h8000000000000000); csr(1,'h306,2);
    csr(1,'h60a,64'h8000000000000000); csr(1,'h606,2);
    csr(1,'h603,'h40); csr(1,'h604,'h40); enter_guest();
    assert (!wfi_wake && !interrupt_request) else $fatal(1,"early guest timer");
    time_counter = 101; #1;
    assert (wfi_wake && !interrupt_request) else $fatal(1,"Sstc WFI/global-enable mismatch");
    csr(0,'h144,0,'h20); csr(1,'h100,2);
    take_interrupt('h3000,1,1,5);
    csr(1,'h14d,200); command(4,'habc); check_context(1,1);
    assert (!wfi_wake && !interrupt_request) else $fatal(1,"timer rearm failed");
    reset_dut(); time_counter = 100;
  endtask

  task automatic fp_status;
    // All status combinations, including independent SD summaries and both
    // FS=Off gates. Disabled FP CSRs raise illegal, never virtual instruction.
    for (int host_fs = 0; host_fs < 4; host_fs++) begin
      for (int guest_fs = 0; guest_fs < 4; guest_fs++) begin
        reset_dut();
        csr(1, 'h200, 64'(guest_fs) << 13);
        csr(0, 'h200, 0, 64'h200000000 | (64'(guest_fs) << 13) |
            (guest_fs == 3 ? 64'h8000000000000000 : 0));
        enter_guest(0, 2'(host_fs)); #1;
        assert (fp_enabled == (host_fs != 0 && guest_fs != 0)) else $fatal(1, "FS enable gates");
        assert (mstatus[14:13] == 2'(host_fs) && mstatus[63] == (host_fs == 3))
          else $fatal(1, "host FS/SD changed by guest status");
        csr(0, 'h100, 0, 64'h200000000 | (64'(guest_fs) << 13) |
            (guest_fs == 3 ? 64'h8000000000000000 : 0));
        if (host_fs == 0 || guest_fs == 0) denied_csr('h003, 2);
        else csr(0, 'h003, 0, 0);
      end
    end

    reset_dut(); csr(1, 'h200, 'h4000); enter_guest(0, 1);
    csr(1, 'h002, 3); // shared frm write dirties both status banks
    csr(0, 'h003, 0, 'h60);
    csr(0, 'h100, 0, 64'h8000000200006000);
    assert (mstatus[14:13] == 3 && mstatus[63]) else $fatal(1, "guest CSR write did not dirty HS");
    csr(1, 'h100, 'h2000); // guest may clean its own bank, never the host bank
    csr(0, 'h100, 0, 'h200002000);
    assert (mstatus[14:13] == 3 && mstatus[63]) else $fatal(1, "guest cleaned HS FS");
    @(negedge clock); fp_update_in = {1'b1, 5'b00101};
    @(posedge clock); #1; fp_update_in = 0;
    csr(0, 'h003, 0, 'h65); csr(0, 'h100, 0, 64'h8000000200006000);
    command(1, 'h2000, 1); // trap must retain dirty state and shared flags
    csr(0, 'h003, 0, 'h65); csr(0, 'h200, 0, 64'h8000000200006000);
    csr(1, 'h200, 'h2000); csr(1, 'h100, 'h4100);
    csr(1, 'h001, 0); // host modification must leave guest FS clean
    csr(0, 'h200, 0, 'h200002000);
    command(4, 'h804); check_context(1, 1);
    csr(0, 'h003, 0, 'h60); csr(0, 'h100, 0, 'h200002000);

    // VU uses both gates too, but fcsr is an unprivileged shared CSR.
    reset_dut(); csr(1, 'h200, 'h2000); enter_guest(1, 1);
    csr(1, 'h001, 1); csr(0, 'h003, 0, 1);
    command(1, 'h2000, 1); csr(0, 'h200, 0, 64'h8000000200006000);
  endtask

  task automatic vector_status;
    for (int host_vs = 0; host_vs < 4; host_vs++) begin
      for (int guest_vs = 0; guest_vs < 4; guest_vs++) begin
        reset_dut(); csr(1,'h200,64'(guest_vs) << 9);
        enter_guest(0,0,2'(host_vs)); #1;
        assert (vector_enabled == (host_vs != 0 && guest_vs != 0)) else $fatal(1,"VS enable gates");
        assert (mstatus[10:9] == 2'(host_vs) && mstatus[63] == (host_vs == 3)) else $fatal(1,"independent HS VS/SD");
        csr(0,'h100,0,64'h200000000 | (64'(guest_vs) << 9) | (guest_vs == 3 ? 64'h8000000000000000 : 0));
        if (host_vs == 0 || guest_vs == 0) denied_csr('hc22,2);
        else csr(0,'hc22,0,16);
      end
    end
    reset_dut(); csr(1,'h200,'h200); enter_guest(0,0,1);
    csr(1,'h00a,2); csr(0,'h00f,0,4);
    csr(0,'h100,0,64'h8000000200000600);
    assert (mstatus[10:9] == 3 && !fp_enabled) else $fatal(1,"guest vector write did not dirty HS");
    csr(1,'h100,'h200); csr(0,'h100,0,'h200000200);
    assert (mstatus[10:9] == 3 && mstatus[63]) else $fatal(1,"guest cleaned host vector state");
    command(1,'h2000,1); csr(0,'h00f,0,4);
    csr(1,'h00a,1); csr(0,'h200,0,'h200000200);
    command(4,'h804); csr(0,'h00f,0,2); // vector CSR state is shared, not banked
    reset_dut(); csr(1,'h200,'h200); enter_guest(1,0,1);
    csr(1,'h008,3); csr(0,'h008,0,3);
    command(1,'h2000,1); csr(0,'h200,0,64'h8000000200000600);
  endtask

  task automatic environment_controls;
    reset_dut();
    csr(1,'h30a,64'h4000000000000000,0,1);
    csr(1,'h60a,64'h40000000000000f1,0,1);
    csr(0,'h60a,0,64'h40000000000000f1);
    assert (pbmte && guest_translation.vs_pbmte) else $fatal(1,"VS PBMTE missing");
    csr(1,'h60a,64'h40000000000000f1,0,0);
    csr(1,'h30a,0,0,1); csr(0,'h60a,0,'hf1);
    assert (!pbmte && !guest_translation.vs_pbmte) else $fatal(1,"M PBMTE failed to mask H");
    csr(1,'h30a,64'h4000000000000000,0,1);
    csr(1,'h60a,'hf1,0,1);
    assert (pbmte && !guest_translation.vs_pbmte) else $fatal(1,"independent stage enables");
    reset_dut(); csr(0,'h60a,0,0);
    csr(1,'h60a,64'hffffffffffffffff); csr(0,'h60a,0,64'h3000000f1);
    csr(1,'h60a,'he1); csr(0,'h60a,0,'hc1); // reserved CBIE becomes Disabled
    csr(1,'h60a,'hf1); csr(1,'h30a,0); csr(0,'h60a,0,'hf1); // controls do not mask other CSR storage
    csr(1,'h30a,'hf1); csr(1,'h10a,'hf1); csr(1,'h60a,0);
    csr(1,'h300,'h800); csr(1,'h341,'h800); command(3,'h800);
    assert (cbo_permission[3:2] == 0 && cbo_zero_access == 0) else $fatal(1,"henvcfg restricted HS");
    csr(1,'h60a,'hf1); csr(0,'h60a,0,'hf1); // HS owns H CSR

    reset_dut(); csr(1,'h30a,'hf1); csr(1,'h60a,0); enter_guest();
    assert (cbo_permission[3:2] == 2 && cbo_zero_access == 2) else $fatal(1,"HS denial must be virtual");
    denied_csr('h60a,22);

    reset_dut(); csr(1,'h30a,0); csr(1,'h60a,0); enter_guest();
    assert (cbo_permission[3:2] == 1 && cbo_zero_access == 1) else $fatal(1,"machine denial must win");

    reset_dut(); csr(1,'h30a,'hf1); csr(1,'h60a,'hf1); csr(1,'h10a,0); enter_guest();
    assert (cbo_permission[3:2] == 0 && cbo_zero_access == 0) else $fatal(1,"senvcfg restricted VS");
    csr(1,'h10a,'hd1); csr(0,'h10a,0,'hd1); // shared, not substituted to a VS bank
    command(1,'h2000,1); csr(0,'h10a,0,'hd1);

    reset_dut(); csr(1,'h30a,'hf1); csr(1,'h60a,'hf1); csr(1,'h10a,0); enter_guest(1);
    assert (cbo_permission[3:2] == 2 && cbo_zero_access == 2) else $fatal(1,"VU senvcfg denial must be virtual");
    reset_dut(); csr(1,'h30a,'hf1); csr(1,'h60a,'hd1); csr(1,'h10a,'hf1); enter_guest(1);
    assert (cbo_permission == 4'b0010 && cbo_zero_access == 0) else $fatal(1,"guest invalidate must become flush");
  endtask

  task automatic pointer_controls;
    reset_dut();
    for (int mode = 0; mode < 4; mode++) begin
      automatic int legal_mode = mode == 1 ? 0 : mode;
      csr(1,'h60a,64'(mode)<<32);
      csr(0,'h60a,0,64'(legal_mode)<<32);
      csr(1,'h600,64'(mode)<<48);
      csr(0,'h600,0,(64'(legal_mode)<<48)|64'h200000000);
    end
    csr(1,'h60a,64'h200000000,0,0,1);
    csr(1,'h60a,64'h200000000,0,0,0);
    csr(1,'h600,64'h2000000000100,0,0,1);
    csr(1,'h600,64'h2000000000100,0,0,0);
    csr(1,'h10a,64'h300000000,0,0,1);
    csr(1,'h280,64'h8000000000000008);
    enter_guest();
    assert (pointer_masking == 3'b101) else $fatal(1,"VS did not select H PMM");
    csr(1,'h10a,64'h200000000,0,0,1);
    csr(0,'h10a,0,64'h200000000);
    // The shared senvcfg survives exit, and the host can independently update
    // the U-mode HLV/HSV mask without changing its ordinary U/VU mask.
    command(1,'h2000,1);
    csr(0,'h10a,0,64'h200000000);
    csr(1,'h600,64'h3000000000200,0,0,1);
    csr(1,'h100,0);
    csr(1,'h141,'h800); command(4,'h800);
    check_context(0,0);
    assert (pointer_masking == 3'b100 && guest_pointer_masking == 3'b111)
      else $fatal(1,"host U and explicit VU controls were conflated");
  endtask

  task automatic state_enables;
    logic [63:0] mask;
    reset_dut(0);
    for (int i = 0; i < 4; i++) begin
      mask = i == 0 ? 64'hc000000000000000 : 64'h8000000000000000;
      csr(0,12'('h30c+i),0,0); csr(0,12'('h60c+i),0,0);
      csr(1,12'('h60c+i),'1); csr(0,12'('h60c+i),0,0);
      csr(1,12'('h30c+i),'1); csr(0,12'('h30c+i),0,mask);
      csr(0,12'('h60c+i),0,0); // writes to masked bits had no effect
      csr(1,12'('h60c+i),'1); csr(0,12'('h60c+i),0,mask);
      csr(1,12'('h30c+i),0); csr(0,12'('h60c+i),0,0);
      csr(1,12'('h30c+i),mask); csr(0,12'('h60c+i),0,mask);
      csr(1,12'('h10c+i),'1); csr(0,12'('h10c+i),0,0);
    end
    // Each register has its own SE gate, including otherwise-empty pairs 1..3.
    for (int i = 0; i < 4; i++) begin
      for (int gates = 0; gates < 4; gates++) begin
        for (int wr = 0; wr < 2; wr++) begin
          reset_dut(0);
          csr(1,12'('h30c+i),64'h8000000000000000);
          csr(1,12'('h60c+i),gates[1] ? 64'h8000000000000000 : 0);
          csr(1,12'('h30c+i),gates[0] ? 64'h8000000000000000 : 0);
          enter_guest();
          if (!gates[0]) denied_csr(12'('h10c+i),2,1'(wr));
          else if (!gates[1]) denied_csr(12'('h10c+i),22,1'(wr));
          else csr(1'(wr),12'('h10c+i),'1,0);
        end
      end
      reset_dut(0); enter_guest(); denied_csr(12'('h60c+i),2);
      reset_dut(); enter_guest(); denied_csr(12'('h60c+i),22);
      reset_dut(0); csr(1,'h300,'h800); csr(1,'h341,'h800); command(3,'h800);
      denied_csr(12'('h60c+i),2);
    end
    // ENVCFG is independent of SE: deny reads/writes without changing senvcfg.
    for (int gates = 0; gates < 4; gates++) begin
      for (int wr = 0; wr < 2; wr++) begin
        reset_dut(); csr(1,'h10a,1);
        csr(1,'h60c,gates[1] ? 64'h4000000000000000 : 0);
        csr(1,'h30c,gates[0] ? 64'h4000000000000000 : 0);
        enter_guest();
        if (!gates[0]) denied_csr('h10a,2,1'(wr));
        else if (!gates[1]) denied_csr('h10a,22,1'(wr));
        else begin csr(1'(wr),'h10a,0,1); command(1,'h2000,1); end
        if (gates[0]) csr(0,'h10a,0,(gates == 3 && wr != 0) ? 0 : 1);
      end
    end
    // A parent mask affects visibility, not other implemented bits or M access.
    reset_dut(); csr(1,'h30c,64'h8000000000000000);
    csr(0,'h60c,0,64'h8000000000000000);
    csr(1,'h60a,1); csr(0,'h60a,0,1); // M bypasses lower-privilege gates
    enter_guest(); denied_csr('h60a,2);
    reset_dut(); enter_guest(); denied_csr('h60a,22);
    reset_dut(); enter_guest(1); denied_csr('h10a,22);
    reset_dut(0); enter_guest(1); denied_csr('h10a,2);
    reset_dut(); enter_guest(1); denied_csr('h10c,22);
    reset_dut(0); enter_guest(1); denied_csr('h10c,2);
    // State-enable gates do not replace FS/VS or timer STCE/TM controls.
    reset_dut(0); csr(1,'h200,'h2200); enter_guest(0,1,1);
    csr(1,'h003,1); csr(0,'h003,0,1); csr(0,'hc22,0,16);
    reset_dut();
  endtask

  task automatic invalidation_permissions;
    logic [31:0] instruction;
    logic [2:0] action;
    bit guest, user_mode, denied;
    for (int op = 0; op < 5; op++) begin
      case (op)
        0: begin instruction = 'h16000073; action = 3; end
        1: begin instruction = 'h18000073; action = 6; end
        2: begin instruction = 'h18100073; action = 6; end
        3: begin instruction = 'h26000073; action = 4; end
        4: begin instruction = 'h66000073; action = 5; end
      endcase
      for (int mode = 0; mode < 5; mode++) begin
        for (int controls = 0; controls < 4; controls++) begin
          guest = mode >= 3; user_mode = mode inside {2,4};
          denied = mode != 0 && (user_mode || (guest && op >= 3) ||
                   (op == 0 && (guest ? controls[1] : controls[0])) ||
                   (op == 4 && !guest && controls[0]));
          reset_dut(0);
          csr(1,'h600,controls[1] ? 'h100000 : 0);
          csr(1,'h300,(guest ? MPV : 0) | (user_mode ? 0 : mode == 0 ? 'h1800 : 'h800) | (controls[0] ? 'h100000 : 0));
          if (mode != 0) begin csr(1,'h341,'h800); command(3,'h800); end
          @(negedge clock); clear_commit();
          commit_in.valid = 1; commit_in.bits.pc = 'h888;
          commit_in.bits.instruction = instruction; commit_in.bits.fence.action = action;
          #1;
          assert (retired == !denied && !writeback_valid && redirect_out.valid == denied &&
                  translation_flush == (!denied && action != 6))
            else $fatal(1,"Svinval permission op=%0d mode=%0d controls=%0d",op,mode,controls);
          @(posedge clock); #1; clear_commit();
          if (denied) begin
            csr(0,'h142,0,guest ? 22 : 2);
            csr(0,'h141,0,'h888); csr(0,'h143,0,64'(instruction));
          end
        end
      end
    end
    $display("100 Svinval privilege/TVM/VTVM cases passed");
  endtask

  task automatic sha_csr_contracts;
    logic [63:0] replacement, expected, vector_base, cause, value;
    reset_dut();
    csr(0,'h301,0,64'h80000000003411ab);
    csr(1,'h301,0); csr(0,'h301,0,64'h80000000003411ab);
    // Shcounterenw: implemented base counters have writable enables; every
    // unimplemented HPM counter is hardwired zero, with its enable also zero.
    csr(1,'h606,'1); csr(0,'h606,0,7);
    csr(1,'h606,0); csr(0,'h606,0,0);
    for (int index = 3; index < 32; index++) begin
      csr(1,12'('hb00+index),'1); csr(0,12'('hb00+index),0,0);
    end
    // Shvsatpa/Shgatpa: sweep every mode encoding and verify unsupported
    // writes preserve the whole register, not just its mode field.
    for (int mode = 0; mode < 16; mode++) begin
      for (int bank = 0; bank < 3; bank++) begin
        csr(1,bank == 0 ? 12'h180 : bank == 1 ? 12'h280 : 12'h680,64'h8000000000001234);
        replacement = (64'(mode) << 60) | 64'h0ffff0001234567b;
        expected = mode inside {0,8} ? (64'(mode) << 60) | 64'h1234567b : 64'h8000000000001234;
        if (bank == 2) expected &= ~64'd3;
        csr(1,bank == 0 ? 12'h180 : bank == 1 ? 12'h280 : 12'h680,replacement);
        csr(0,bank == 0 ? 12'h180 : bank == 1 ? 12'h280 : 12'h680,0,expected);
      end
    end
    // Shvstvecd: retain each meaningful address bit, including bit 2, and
    // actually take a delegated trap using that direct vector address.
    for (int bit_index = 2; bit_index <= 38; bit_index++) begin
      reset_dut();
      vector_base = bit_index == 38 ? 64'hffffffc000000000 : 64'd1 << bit_index;
      csr(1,'h205,vector_base); csr(0,'h205,0,vector_base);
      csr(1,'h602,1 << 8); enter_guest(1);
      command(1,vector_base,1); check_context(1,1);
    end
    // Shvstvala: VS trap entry retains full address/instruction values for
    // each supported address-bearing exception, independent of host stval.
    for (int index = 0; index < 11; index++) begin
      case (index)
        0: cause=0; 1: cause=1; 2: cause=2; 3: cause=3; 4: cause=4;
        5: cause=5; 6: cause=6; 7: cause=7; 8: cause=12; 9: cause=13; 10: cause=15;
      endcase
      value = cause == 2 ? 64'hffffffff : 64'hffffffc01234567a;
      reset_dut(); csr(1,'h143,'h123); csr(1,'h602,64'd1 << cause); enter_guest();
      @(negedge clock); clear_commit();
      commit_in.valid=1; commit_in.bits.pc='h888;
      commit_in.bits.exception_valid=1; commit_in.bits.exception_cause=cause;
      commit_in.bits.exception_value=value;
      #1;
      assert (!retired && redirect_out.valid && redirect_out.bits == 'h3000)
        else $fatal(1,"VS trap destination cause=%0d",cause);
      @(posedge clock); #1; clear_commit(); check_context(1,1);
      csr(0,'h142,0,cause); csr(0,'h143,0,value); csr(0,'h141,0,'h888);
      command(1,'h2000,1);
      csr(0,'h243,0,value); csr(0,'h241,0,'h888);
    end
    // Shtvala storage must retain the full Sv39x4 GPA, including bit 40,
    // for all three guest-page-fault causes and both provenance classes.
    for (int index=0; index<3; index++)
      for (int controls=0; controls<4; controls++) begin
        reset_dut();
        if (controls[1]) csr(1,'h302,0);
        enter_guest();
        guest_fault(index == 0 ? 20 : index == 1 ? 21 : 23,controls[0],controls[1],64'h1ffffffeff8);
      end
    $display("Sha CSR mode, vector, counter, and trap-value contracts passed");
  endtask

  initial begin
    // The fixed MISA includes B and full V (and H), independently of FS/VS.
    reset_dut();
    csr(0, 'h301, 0, 64'h80000000003411ab);
    csr(1, 'h301, 0);
    csr(0, 'h301, 0, 64'h80000000003411ab);
    csr(1, 'h301, ~64'd0);
    csr(0, 'h301, 0, 64'h80000000003411ab);
    sha_csr_contracts();
    invalidation_permissions();
    state_enables();
    supervisor_timers();
    pointer_controls();
    environment_controls();
    vector_status();
    fp_status();
    virtual_interrupts();
    // Paged roots retain only supported mode/PPN fields. Unsupported modes
    // preserve the entire old register; HGATP always aligns its x4 root.
    reset_dut();
    csr(1, 'h280, 64'h8ffff00012345678);
    csr(0, 'h280, 0, 64'h8000000012345678);
    csr(1, 'h280, 64'h9000000000000000);
    csr(0, 'h280, 0, 64'h8000000012345678);
    csr(1, 'h680, 64'h8ffff0001234567b);
    csr(0, 'h680, 0, 64'h8000000012345678);
    csr(1, 'h680, 64'h9000000000000000);
    csr(0, 'h680, 0, 64'h8000000012345678);
    fence(4, 1); fence(5, 1);
    // TVM affects GVMA in HS, never VVMA. Both trap virtually in VS.
    csr(1, 'h300, 'h100800); csr(1, 'h341, 'h800); command(3, 'h800);
    fence(4, 1); fence(5, 0, 2);
    reset_dut(); enter_guest(); fence(4, 0, 22);
    reset_dut(); enter_guest(); fence(5, 0, 22);

    // M -> HS -> VS and independent HS/VS aliases.
    reset_dut();
    csr(1, 'h140, 'h1111);
    csr(1, 'h240, 'h2222);
    csr(1, 'h300, 'h800);
    csr(1, 'h341, 'h400);
    command(3, 'h400); check_context(1, 0);
    csr(0, 'h600, 0, 'h200000000);
    csr(1, 'h600, 'h180);
    csr(1, 'h100, 'h100);
    csr(1, 'h141, 'h800);
    command(4, 'h800); check_context(1, 1);
    csr(0, 'h140, 0, 'h2222);
    csr(1, 'h140, 'h3333);
    csr(1, 'h180, 'hffffffffffffffff); csr(0, 'h180, 0, 0);
    command(1, 'h2000, 1); check_context(1, 0);
    csr(0, 'h142, 0, 10);
    csr(0, 'h140, 0, 'h1111);
    csr(0, 'h240, 0, 'h3333);
    csr(0, 'h600, 0, 'h200000180);
    command(4, 'h804); check_context(1, 1);

    // VU ECALL delegated to VS; VS SRET restores VU without leaving the guest.
    reset_dut();
    csr(1, 'h602, 'hffffff);
    // Guest ECALL and guest-page/virtual-instruction traps cannot delegate to VS.
    csr(0, 'h602, 0, 'hcb1ff);
    csr(1, 'h200, 2);
    enter_guest(1);
    command(1, 'h3000, 1); check_context(1, 1);
    csr(0, 'h142, 0, 8);
    csr(0, 'h141, 0, 'h804);
    csr(0, 'h100, 0, 'h200000020);
    command(4, 'h804); check_context(0, 1);
    command(4, 'h2000, 1); check_context(1, 0);
    csr(0, 'h142, 0, 22);
    csr(0, 'h143, 0, 'h10200073);

    // VTVM virtual instruction takes priority over the HS-only TVM restriction.
    reset_dut();
    csr(1, 'h600, 'h100000);
    csr(1, 'h300, MPV | 'h700800);
    csr(1, 'h341, 'h800);
    command(3, 'h800);
    @(negedge clock); clear_commit();
    commit_in.valid = 1; commit_in.bits.rd = 1; commit_in.bits.system.csr = 2;
    commit_in.bits.csr_address = 'h180; commit_in.bits.instruction = 'h180020f3;
    #1;
    assert (!retired && !writeback_valid && !translation_flush && redirect_out.bits == 'h2000)
      else $fatal(1, "VTVM access escaped virtual-instruction trap");
    @(posedge clock); #1; clear_commit();
    csr(0, 'h142, 0, 22); csr(0, 'h143, 0, 'h180020f3);

    // Captured GPA, GVA, and implicit VS PTE-read provenance survive trap entry.
    reset_dut(); enter_guest(); guest_fault(21, 1, 0);
    csr(0, 'h600, 0, 'h2000001c0);
    command(4, 'h880); check_context(1, 1);
    reset_dut(); csr(1, 'h302, 0); enter_guest(); guest_fault(23, 0, 1);
    assert ((mstatus & (MPV | GVA)) == (MPV | GVA)) else $fatal(1, "M trap lost saved V/GVA");
    command(3, 'h880); check_context(1, 1);

    // Counter gating distinguishes M restrictions (illegal) and H restrictions (virtual).
    reset_dut(); csr(1, 'h306, 7); csr(1, 'h606, 7); csr(1, 'h605, 25);
    enter_guest(); csr(0, 'hc01, 0, 125);
    command(1, 'h2000, 1); csr(0, 'hc01, 0, 100);
    reset_dut(); csr(1, 'h306, 7); enter_guest(); denied_csr('hc01, 22);
    reset_dut(); enter_guest(); denied_csr('hc01, 2);
    reset_dut(); enter_guest(); denied_csr('h600, 22);
    reset_dut(); enter_guest(1); denied_csr('h200, 22);
    reset_dut(); enter_guest(); denied_csr('h300, 2);
    reset_dut(); enter_guest(); denied_csr('h6ff, 2);
    reset_dut(); csr(1, 'h306, 7); enter_guest(); denied_csr('hc01, 2, 1);
    reset_dut(); csr(1, 'h306, 7); enter_guest(); denied_csr('hc81, 2);

    // Guest SRET is controlled by VTSR, not the host's TSR bit.
    reset_dut(); csr(1, 'h600, 'h400000); enter_guest();
    command(4, 'h2000, 1); csr(0, 'h142, 0, 22);
    reset_dut(); csr(1, 'h241, 'h900);
    csr(1, 'h300, MPV | 'h400800); csr(1, 'h341, 'h800);
    command(3, 'h800); command(4, 'h900); check_context(0, 1);

    // A host supervisor interrupt preempts VS even with HS SIE clear.
    reset_dut(); csr(1, 'h303, 'h20); csr(1, 'h304, 'h20); enter_guest();
    @(negedge clock); clear_commit(); interrupts = 6'b001000;
    interrupt_boundary = 1; interrupt_pc = 'habc;
    #1;
    assert (interrupt_request && redirect_out.valid && redirect_out.bits == 'h2000)
      else $fatal(1, "host interrupt did not preempt guest");
    @(posedge clock); #1; clear_commit(); interrupts = 0; check_context(1, 0);
    csr(0, 'h141, 0, 'habc); csr(0, 'h142, 0, 'h8000000000000005);
    csr(0, 'h643, 0, 0); csr(0, 'h64a, 0, 0);

    // CSR side effects require a nonspeculative, nontrapping commit.
    reset_dut();
    @(negedge clock); clear_commit();
    commit_in.bits.system.csr = 1; commit_in.bits.csr_address = 'h240;
    commit_in.bits.csr_source = 'hbad;
    repeat (2) @(posedge clock);
    #1; clear_commit(); csr(0, 'h240, 0, 0);
    @(negedge clock); clear_commit();
    commit_in.valid = 1; commit_in.bits.system.csr = 1;
    commit_in.bits.csr_address = 'h240; commit_in.bits.csr_source = 'hbad;
    commit_in.bits.exception_valid = 1; commit_in.bits.exception_cause = 2;
    @(posedge clock); #1; clear_commit(); csr(0, 'h240, 0, 0);
    $display("rv5stage hypervisor CSR PASS");
    $finish;
  end
  initial begin #100000; $fatal(1, "timeout"); end
endmodule
