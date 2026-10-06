// Checks nested VS/G walks, stage permissions, precise faults, and canceled-response ownership.
// SPDX-License-Identifier: Apache-2.0
module riscv_nested_walker_tb;
  reg clock = 0;
  always #5 clock = ~clock;
  reg reset = 1, virtualized = 1;
  reg command_valid = 0, vs_mode = 0, g_mode = 0;
  reg [63:0] address = 0;
  reg [1:0] access = 1, privilege = 1;
  reg [43:0] vs_root = 1, g_root = 4;
  reg vs_sum = 0, vs_mxr = 0, hs_mxr = 0, vs_pbmte = 0, g_pbmte = 0;
  reg cancel = 0, memory_ready = 0, memory_fault = 0, response_valid = 0, completion_ready = 0;
  reg [63:0] pte = 0;
  wire command_ready, memory_valid, completed, implicit_read, gva;
  wire [63:0] memory_address, result_virtual, result_guest, fault_guest, vs_leaf, g_leaf;
  wire [55:0] result_address;
  wire [1:0] fault, result_access;
  wire [1:0] memory_pbmt;
  RiscvNestedWalkerFixture dut(.*);
  localparam NONE = 0, PAGE = 1, GUEST = 2, PHYSICAL = 3;
  localparam FETCH = 0, LOAD = 1, STORE = 2;
  localparam [63:0] VR = 'h43, VW = 'hc7, GX = 'h59, GR = 'h53, GW = 'hd7;
  integer reads = 0, checks = 0;

  task automatic tick;
    @(posedge clock); #1;
  endtask
  task automatic falling;
    @(negedge clock); #1;
  endtask
  function automatic [63:0] entry(input [43:0] ppn, input [63:0] flags);
    return {10'b0, ppn, 10'b0} | flags;
  endfunction
  task automatic defaults;
    vs_mode = 0; g_mode = 0; vs_root = 1; g_root = 4;
    privilege = 1; vs_sum = 0; vs_mxr = 0; hs_mxr = 0;
    vs_pbmte = 0; g_pbmte = 0;
  endtask
  task automatic launch(input [63:0] va, input [1:0] kind);
    address = va; access = kind; command_valid = 1; #1;
    if (!command_ready) $fatal(1, "walker not available");
    tick(); falling(); command_valid = 0;
  endtask
  task automatic wait_read(input [63:0] expected, input [1:0] expected_pbmt = 0);
    integer timeout;
    timeout = 0;
    while (!memory_valid && timeout < 60) begin tick(); timeout++; end
    if (!memory_valid || memory_address !== expected || memory_pbmt !== expected_pbmt)
      $fatal(1, "PTE request expected %h, got valid=%b address=%h", expected, memory_valid, memory_address);
    checks++;
    // Backpressure must preserve the physical address and validity.
    repeat (2) begin
      tick();
      if (!memory_valid || memory_address !== expected || memory_pbmt !== expected_pbmt) $fatal(1, "unstable PTE offer");
    end
    falling();
  endtask
  task automatic reply(input [63:0] expected, input [63:0] value, input [1:0] expected_pbmt = 0);
    wait_read(expected, expected_pbmt);
    memory_ready = 1; tick(); falling(); memory_ready = 0; reads++;
    tick(); falling(); pte = value; response_valid = 1;
    tick(); falling(); response_valid = 0;
  endtask
  task automatic translate_g(input [63:0] gpa, input [63:0] flags);
    reply('h4000 + (((gpa >> 30) & 'h7ff) << 3), entry(8, 1));
    reply('h8000 + (((gpa >> 21) & 'h1ff) << 3), entry(9, 1));
    reply('h9000 + (((gpa >> 12) & 'h1ff) << 3), entry(44'h100 + 44'(gpa >> 12), flags));
  endtask
  task automatic finish(input [1:0] expected_fault, input [63:0] va,
                        input [1:0] kind, input [55:0] pa,
                        input [63:0] fault_gpa = 0, input bit implicit_pte = 0);
    integer timeout;
    timeout = 0;
    while (!completed && timeout < 60) begin tick(); timeout++; end
    if (!completed || fault !== expected_fault || result_virtual !== va || result_access !== kind)
      $fatal(1, "completion expected fault=%d va=%h access=%d, got valid=%b fault=%d va=%h access=%d", expected_fault, va, kind, completed, fault, result_virtual, result_access);
    if (expected_fault == NONE && result_address !== pa) $fatal(1, "physical address expected %h got %h", pa, result_address);
    if (expected_fault == GUEST && (fault_guest !== fault_gpa || implicit_read !== implicit_pte || !gva))
      $fatal(1, "guest provenance expected GPA=%h implicit=%b got GPA=%h implicit=%b", fault_gpa, implicit_pte, fault_guest, implicit_read);
    repeat (3) begin
      tick();
      if (!completed || fault !== expected_fault || result_virtual !== va || memory_valid) $fatal(1, "unstable completion");
    end
    falling(); completion_ready = 1; tick(); falling(); completion_ready = 0;
    checks++;
  endtask
  task automatic cancel_now;
    cancel = 1; tick(); falling(); cancel = 0; #1;
  endtask

  initial begin
    repeat (3) tick(); falling(); reset = 0;
    defaults(); launch('h123456, LOAD); finish(NONE, 'h123456, LOAD, 'h123456);
    // Bare is not sign-extension checked; physical overflow must not truncate.
    defaults(); launch(64'h8000000000, FETCH); finish(NONE, 64'h8000000000, FETCH, 56'h8000000000);
    defaults(); launch(64'h100000000000000, LOAD); finish(PHYSICAL, 64'h100000000000000, LOAD, 0);
    defaults(); vs_mode = 1; launch(64'h8000000000, LOAD); finish(PAGE, 64'h8000000000, LOAD, 0);

    // VS only, including a global nonleaf and changed live caller context.
    defaults(); vs_mode = 1; launch('h1234, LOAD);
    defaults(); vs_root = 99; privilege = 0;
    reply('h1000, entry(2, 'h21)); reply('h2000, entry(3, 1)); reply('h3008, entry(5, VR));
    while (!completed) tick();
    if (!vs_leaf[5] || g_leaf !== 0 || result_guest !== 'h5234) $fatal(1, "VS leaf or Bare G snapshot");
    finish(NONE, 'h1234, LOAD, 'h5234);

    // Sv39x4 uses an 11-bit root index, not an Sv39 9-bit index.
    defaults(); g_mode = 1; launch(64'h10000001234, LOAD);
    reply('h6000, entry(8, 1)); reply('h8000, entry(9, 1)); reply('h9008, entry('h222, GR | 'h20));
    while (!completed) tick();
    if (g_leaf[5] || vs_leaf !== 0) $fatal(1, "G global bit must be ignored");
    finish(NONE, 64'h10000001234, LOAD, 'h222234);
    defaults(); g_mode = 1; launch(64'h20000000000, LOAD); finish(GUEST, 64'h20000000000, LOAD, 0, 64'h20000000000);
    defaults(); g_mode = 1; g_root = 5; launch('h1234, LOAD); finish(GUEST, 'h1234, LOAD, 0, 'h1234);
    defaults(); vs_mode = 1; g_mode = 1; vs_root = 44'h20000000; launch('h1234, LOAD);
    finish(GUEST, 'h1234, LOAD, 0, 64'h20000000000, 1);

    // Full cold walk: three translated VS PTE reads and one final G walk.
    defaults(); vs_mode = 1; g_mode = 1; reads = 0; launch('h1234, STORE);
    translate_g('h1000, GR); reply('h101000, entry(2, 1));
    translate_g('h2000, GR); reply('h102000, entry(3, 1));
    translate_g('h3008, GR); reply('h103008, entry(5, VW));
    translate_g('h5234, GW);
    finish(NONE, 'h1234, STORE, 'h105234);
    if (reads != 15) $fatal(1, "nested walk bypassed a stage");

    // VS 2 MiB leaf, G 1 GiB leaf: stage offsets compose independently.
    defaults(); vs_mode = 1; g_mode = 1; launch('h1234, LOAD);
    reply('h4000, entry('h40000, GR)); reply('h40001000, entry(2, 1));
    reply('h4000, entry('h40000, GR)); reply('h40002000, entry('h200, VR));
    reply('h4000, entry('h40000, GR)); finish(NONE, 'h1234, LOAD, 'h40201234);

    // Implicit PTE failures preserve original access and the PTE GPA.
    for (int kind = 0; kind < 3; kind++) begin
      defaults(); vs_mode = 1; g_mode = 1; launch('h1234, kind[1:0]);
      reply('h4000, 0); finish(GUEST, 'h1234, kind[1:0], 0, 'h1000, 1);
    end
    defaults(); vs_mode = 1; g_mode = 1; launch('h1234, FETCH);
    reply('h4000, entry(0, GR)); reply('h1000, 0); finish(PAGE, 'h1234, FETCH, 0);
    defaults(); vs_mode = 1; g_mode = 1; launch('h1234, LOAD);
    reply('h4000, entry(0, GR)); reply('h1000, entry(0, VR));
    reply('h4000, 0); finish(GUEST, 'h1234, LOAD, 0, 'h1234);

    // MXR relaxes explicit loads only, never an implicit VS page-table read.
    // Preserve the original access cause and PTE GPA for every MXR setting.
    for (int mxr_bits = 0; mxr_bits < 4; mxr_bits++) begin
      for (int kind = 0; kind < 3; kind++) begin
        defaults(); vs_mode = 1; g_mode = 1;
        hs_mxr = mxr_bits[0]; vs_mxr = mxr_bits[1];
        launch('h1234, kind[1:0]);
        reply('h4000, entry(0, GX)); finish(GUEST, 'h1234, kind[1:0], 0, 'h1000, 1);
      end
    end

    // G requires U even for VS PTE reads. VS MXR cannot relax G permissions.
    defaults(); vs_mode = 1; g_mode = 1; launch('h1234, LOAD);
    reply('h4000, entry(0, VR)); finish(GUEST, 'h1234, LOAD, 0, 'h1000, 1);
    defaults(); g_mode = 1; vs_mxr = 1; launch('h1234, LOAD);
    reply('h4000, entry(0, GX)); finish(GUEST, 'h1234, LOAD, 0, 'h1234);
    defaults(); g_mode = 1; hs_mxr = 1; launch('h1234, LOAD);
    reply('h4000, entry(0, GX)); finish(NONE, 'h1234, LOAD, 'h1234);
    defaults(); vs_mode = 1; vs_mxr = 1; launch('h1234, LOAD);
    reply('h1000, entry(0, 'h49)); finish(NONE, 'h1234, LOAD, 'h1234);
    defaults(); vs_mode = 1; hs_mxr = 1; launch('h1234, LOAD);
    reply('h1000, entry(0, 'h49)); finish(NONE, 'h1234, LOAD, 'h1234);
    defaults(); vs_mode = 1; launch('h1234, LOAD);
    reply('h1000, entry(0, GR)); finish(PAGE, 'h1234, LOAD, 0);
    defaults(); vs_mode = 1; vs_sum = 1; launch('h1234, LOAD);
    reply('h1000, entry(0, GR)); finish(NONE, 'h1234, LOAD, 'h1234);
    defaults(); vs_mode = 1; vs_sum = 1; launch('h1234, FETCH);
    reply('h1000, entry(0, GX)); finish(PAGE, 'h1234, FETCH, 0);
    defaults(); vs_mode = 1; privilege = 0; launch('h1234, LOAD);
    reply('h1000, entry(0, VR)); finish(PAGE, 'h1234, LOAD, 0);
    defaults(); vs_mode = 1; privilege = 0; launch('h1234, LOAD);
    reply('h1000, entry(0, GR)); finish(NONE, 'h1234, LOAD, 'h1234);
    defaults(); g_mode = 1; launch('h1234, FETCH);
    reply('h4000, entry(0, GX)); finish(NONE, 'h1234, FETCH, 'h1234);

    // The shared optional NAPOT geometry works independently at either stage.
    defaults(); vs_mode = 1; launch('h1234, LOAD);
    reply('h1000, entry(2, 1)); reply('h2000, entry(3, 1));
    reply('h3008, entry(8, VR | (64'h1 << 63))); finish(NONE, 'h1234, LOAD, 'h1234);
    defaults(); g_mode = 1; launch('h1234, LOAD);
    reply('h4000, entry(8, 1)); reply('h8000, entry(9, 1));
    reply('h9008, entry('h108, GR | (64'h1 << 63))); finish(NONE, 'h1234, LOAD, 'h101234);

    // Svade A/D and malformed PTEs: no implicit hardware write is issued.
    defaults(); g_mode = 1; launch('h1234, STORE);
    reply('h4000, entry(0, GW & ~64'h80)); finish(GUEST, 'h1234, STORE, 0, 'h1234);
    defaults(); vs_mode = 1; launch('h1234, STORE);
    reply('h1000, entry(0, VW & ~64'h80)); finish(PAGE, 'h1234, STORE, 0);
    defaults(); g_mode = 1; launch('h1234, LOAD);
    reply('h4000, entry(0, GR & ~64'h40)); finish(GUEST, 'h1234, LOAD, 0, 'h1234);
    defaults(); vs_mode = 1; launch('h1234, LOAD);
    reply('h1000, entry(1, VR)); finish(PAGE, 'h1234, LOAD, 0);
    defaults(); g_mode = 1; launch('h1234, LOAD);
    reply('h4000, entry(0, GR | (64'h1 << 54))); finish(GUEST, 'h1234, LOAD, 0, 'h1234);
    defaults(); g_mode = 1; launch('h1234, LOAD);
    reply('h4000, entry(1, GR)); finish(GUEST, 'h1234, LOAD, 0, 'h1234);
    defaults(); g_mode = 1; launch('h1234, LOAD);
    reply('h4000, entry(8, 'h41)); finish(GUEST, 'h1234, LOAD, 0, 'h1234);
    defaults(); vs_mode = 1; launch('h1234, LOAD);
    reply('h1000, entry(2, 1)); reply('h2000, entry(3, 1)); reply('h3008, entry(4, 1)); finish(PAGE, 'h1234, LOAD, 0);

    // Independent PBMTE controls and retained PBMT leaf snapshots.
    defaults(); vs_mode = 1; vs_pbmte = 1; launch('h1234, LOAD); vs_pbmte = 0;
    reply('h1000, entry(0, VR | (64'h2 << 61)));
    while (!completed) tick();
    if (vs_leaf[62:61] != 2) $fatal(1, "VS PBMT or captured PBMTE lost");
    finish(NONE, 'h1234, LOAD, 'h1234);
    defaults(); g_mode = 1; vs_pbmte = 1; launch('h1234, LOAD);
    reply('h4000, entry(0, GR | (64'h1 << 61))); finish(GUEST, 'h1234, LOAD, 0, 'h1234);
    defaults(); g_mode = 1; g_pbmte = 1; launch('h1234, LOAD);
    reply('h4000, entry(0, GR | (64'h1 << 61)));
    while (!completed) tick();
    if (g_leaf[62:61] != 1) $fatal(1, "G PBMT lost");
    finish(NONE, 'h1234, LOAD, 'h1234);

    defaults(); vs_mode = 1; g_mode = 1; g_pbmte = 1; launch('h1234, LOAD);
    reply('h4000, entry(0, GR | (64'h1 << 61)));
    reply('h1000, entry(0, VR), 1);
    reply('h4000, entry(0, GR)); finish(NONE, 'h1234, LOAD, 'h1234);

    // Physical PTE rejection owes no response; original access survives.
    defaults(); vs_mode = 1; g_mode = 1; launch('h1234, STORE);
    wait_read('h4000); memory_fault = 1; memory_ready = 1;
    tick(); falling(); memory_fault = 0; memory_ready = 0;
    finish(PHYSICAL, 'h1234, STORE, 0);

    // Zero-latency PTE memory is legal too.
    defaults(); g_mode = 1; launch('h1234, LOAD); wait_read('h4000);
    memory_ready = 1; response_valid = 1; pte = entry(0, GR);
    tick(); falling(); memory_ready = 0; response_valid = 0;
    finish(NONE, 'h1234, LOAD, 'h1234);

    // Cancel before acceptance, while waiting, and on response/completion edges.
    defaults(); g_mode = 1; launch('h1234, LOAD); wait_read('h4000); cancel_now();
    if (!command_ready || memory_valid || completed) $fatal(1, "unaccepted cancel did not release");
    defaults(); g_mode = 1; launch('h1234, LOAD); wait_read('h4000);
    memory_ready = 1; tick(); falling(); memory_ready = 0; cancel_now();
    command_valid = 1; address = 'h5678;
    repeat (3) begin
      tick(); if (command_ready || memory_valid || completed) $fatal(1, "orphan response owner lost");
    end
    falling(); cancel_now();
    if (command_ready) $fatal(1, "repeated cancellation forgot pending PTE");
    response_valid = 1; pte = entry(0, GR); tick(); falling(); response_valid = 0; command_valid = 0;
    if (!command_ready || completed) $fatal(1, "drain did not release silently");
    defaults(); g_mode = 1; launch('h5678, LOAD); reply('h4000, 0); finish(GUEST, 'h5678, LOAD, 0, 'h5678);
    defaults(); g_mode = 1; launch('h1234, LOAD); wait_read('h4000);
    memory_ready = 1; tick(); falling(); memory_ready = 0;
    response_valid = 1; pte = entry(0, GR); cancel_now(); response_valid = 0;
    if (!command_ready || completed) $fatal(1, "cancel on response edge leaked completion");
    defaults(); launch('h1234, LOAD); while (!completed) tick(); falling(); cancel_now();
    if (!command_ready || completed) $fatal(1, "canceled completion escaped");
    command_valid = 1; cancel = 1; #1;
    if (command_ready) $fatal(1, "admitted during cancel");
    tick(); falling(); command_valid = 0; cancel = 0;
    defaults(); launch('h4321, LOAD); finish(NONE, 'h4321, LOAD, 'h4321);
    $display("nested walker PASS (%0d checked transfers/completions)", checks);
    $finish;
  end
  initial begin #200000; $fatal(1, "nested walker watchdog"); end
endmodule
