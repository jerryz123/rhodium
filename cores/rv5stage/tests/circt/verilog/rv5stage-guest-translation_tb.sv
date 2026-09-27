// Checks composed guest TLB reuse, current permissions, precise faults, fences, and canceled walk ownership.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_guest_translation_tb;
  reg clock = 0;
  always #5 clock = ~clock;
  reg reset = 1, virtualized = 1;
  reg command_valid = 0, vs_mode = 1, g_mode = 1;
  reg [63:0] address = 0;
  reg [1:0] access = 1, privilege = 1;
  reg [43:0] vs_root = 1, g_root = 4;
  reg vs_sum = 0, vs_mxr = 0, hs_mxr = 0, vs_pbmte = 0, g_pbmte = 0;
  reg cancel = 0, invalidate_all = 0, memory_ready = 0, memory_fault = 0;
  reg response_valid = 0, completion_ready = 0;
  reg [63:0] pte = 0;
  wire command_ready, memory_valid, completed, implicit_read;
  wire [63:0] memory_address, result_virtual, result_guest, fault_guest, vs_leaf, g_leaf, cause, tinst;
  wire [55:0] result_address;
  wire [1:0] fault, result_access, memory_pbmt;
  wire probe_hit, probe_fault;
  wire [1:0] probe_pbmt;
  RV5StageGuestTranslationFixture dut(.*);
  localparam logic [1:0] NONE = 0, PAGE = 1, GUEST = 2, PHYSICAL = 3;
  localparam logic [1:0] FETCH = 0, LOAD = 1, STORE = 2;
  localparam [63:0] VR = 'h43, VW = 'hc7, GR = 'h53, GW = 'hd7;
  integer checks = 0;

  task automatic tick; @(posedge clock); #1; endtask
  task automatic falling; @(negedge clock); #1; endtask
  function automatic [63:0] entry(input [43:0] ppn, input [63:0] flags);
    return {10'b0, ppn, 10'b0} | flags;
  endfunction
  task automatic defaults;
    vs_mode = 1; g_mode = 1; vs_root = 1; g_root = 4;
    privilege = 1; vs_sum = 0; vs_mxr = 0; hs_mxr = 0; vs_pbmte = 0; g_pbmte = 0;
  endtask
  task automatic fence_all;
    invalidate_all = 1; tick(); falling(); invalidate_all = 0; #1;
  endtask
  task automatic launch(input [63:0] va, input [1:0] kind);
    address = va; access = kind; command_valid = 1; #1;
    if (!command_ready) $fatal(1, "translation not available");
    tick(); falling(); command_valid = 0;
  endtask
  task automatic wait_read(input [63:0] expected);
    integer timeout;
    timeout = 0;
    while (!memory_valid && timeout < 80) begin
      if (completed) $fatal(1, "unexpected cached completion");
      tick(); timeout++;
    end
    if (!memory_valid || memory_address !== expected || memory_pbmt !== 0)
      $fatal(1, "expected PTE %h got %h valid=%b", expected, memory_address, memory_valid);
    repeat (2) begin
      tick(); if (!memory_valid || memory_address !== expected) $fatal(1, "unstable offer");
    end
    falling(); checks++;
  endtask
  task automatic reply(input [63:0] expected, input [63:0] value);
    wait_read(expected); memory_ready = 1; tick(); falling(); memory_ready = 0;
    pte = value; response_valid = 1; tick(); falling(); response_valid = 0;
  endtask
  task automatic nested(input [63:0] vs_flags = VW, input [63:0] g_flags = GW,
                        input [63:0] root = 'h1000);
    reply('h4000, entry('h40000, GR));
    reply('h40000000 + root, entry(0, vs_flags));
    reply('h4000, entry('h40000, g_flags));
  endtask
  task automatic finish(input [1:0] expected_fault, input [63:0] va,
                        input [1:0] kind, input [55:0] pa = 0,
                        input [63:0] gpa = 0, input bit implicit_pte = 0);
    integer timeout;
    reg [63:0] expected_cause;
    timeout = 0;
    while (!completed && timeout < 30) begin
      if (memory_valid) $fatal(1, "unexpected rewalk for %h", va);
      tick(); timeout++;
    end
    if (!completed || fault !== expected_fault || result_virtual !== va || result_access !== kind)
      $fatal(1, "bad completion va=%h fault=%d expected=%d", result_virtual, fault, expected_fault);
    if (expected_fault == NONE && (result_address !== pa || result_guest !== gpa))
      $fatal(1, "wrong composed addresses PA=%h GPA=%h", result_address, result_guest);
    if (expected_fault != NONE) begin
      case (expected_fault)
        PAGE: expected_cause = kind == FETCH ? 12 : kind == LOAD ? 13 : 15;
        GUEST: expected_cause = kind == FETCH ? 20 : kind == LOAD ? 21 : 23;
        default: expected_cause = kind == FETCH ? 1 : kind == LOAD ? 5 : 7;
      endcase
      if (cause !== expected_cause) $fatal(1, "wrong fault class %d expected %d", cause, expected_cause);
    end
    if (expected_fault == GUEST && (fault_guest !== gpa || implicit_read !== implicit_pte || tinst !== (implicit_pte ? 64'h3000 : 64'h0)))
      $fatal(1, "incorrect guest fault provenance GPA=%h implicit=%b tinst=%h", fault_guest, implicit_read, tinst);
    repeat (3) begin
      tick(); if (!completed || result_virtual !== va || fault !== expected_fault || memory_valid) $fatal(1, "unstable held result");
    end
    falling(); completion_ready = 1; tick(); falling(); completion_ready = 0;
    checks++;
  endtask

  initial begin
    repeat (3) tick(); falling(); reset = 0;
    // The SAME bank and walker alternate host and guest at the SAME VA and roots.
    // Only the virtualization tag distinguishes these two cached translations.
    defaults(); g_mode = 0; virtualized = 0;
    launch('h1234, LOAD); reply('h1000, entry('h40000, VW));
    finish(NONE, 'h1234, LOAD, 'h40001234, 'h40001234);
    virtualized = 1;
    launch('h1234, LOAD); reply('h1000, entry('h80000, VW));
    finish(NONE, 'h1234, LOAD, 56'h80001234, 64'h80001234);
    virtualized = 0;
    launch('h1235, LOAD); finish(NONE, 'h1235, LOAD, 'h40001235, 'h40001235);
    // Host superpages keep their reach; composed guest mappings remain 4 KiB.
    launch('h2234, LOAD); finish(NONE, 'h2234, LOAD, 'h40002234, 'h40002234);
    virtualized = 1;
    launch('h1236, LOAD); finish(NONE, 'h1236, LOAD, 56'h80001236, 64'h80001236);
    launch('h2234, LOAD); reply('h1000, entry('h80000, VW));
    finish(NONE, 'h2234, LOAD, 56'h80002234, 64'h80002234);
    fence_all();
    // Cold read then an in-page hit reconstructs PA/GPA using the new offset.
    defaults(); launch('h1234, LOAD); nested(); finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);
    launch('h1fed, STORE); finish(NONE, 'h1fed, STORE, 'h40001fed, 'h1fed);
    // Even a 1 GiB leaf is cached only for the translated 4 KiB slice.
    launch('h2234, LOAD); nested(); finish(NONE, 'h2234, LOAD, 'h40002234, 'h2234);
    launch('h1235, LOAD); finish(NONE, 'h1235, LOAD, 'h40001235, 'h1235);
    // Third slice replaces the oldest of the two entries.
    launch('h3234, LOAD); nested(); finish(NONE, 'h3234, LOAD, 'h40003234, 'h3234);
    launch('h1234, LOAD); nested(); finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);

    // VS and G write/A-D permissions remain separate after a successful load.
    fence_all(); launch('h1234, LOAD); nested(VR, GR); finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);
    launch('h1278, STORE); finish(PAGE, 'h1278, STORE);
    fence_all(); launch('h1234, LOAD); nested(VW, GR); finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);
    launch('h1278, STORE); finish(GUEST, 'h1278, STORE, 0, 'h1278);
    launch('h12ab, FETCH); finish(PAGE, 'h12ab, FETCH);
    fence_all(); launch('h1234, LOAD); nested('h47, GW); finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);
    launch('h1278, STORE); finish(PAGE, 'h1278, STORE);
    fence_all(); launch('h1234, LOAD); nested(VW, 'h57); finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);
    launch('h1278, STORE); finish(GUEST, 'h1278, STORE, 0, 'h1278);
    // Management needs read or write permission and A, but not D.
    launch('h1279, 2'd3); finish(NONE, 'h1279, 2'd3, 'h40001279, 'h1279);
    fence_all(); launch('h1234, LOAD); nested('h4b, GR); finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);
    launch('h12ab, FETCH); finish(GUEST, 'h12ab, FETCH, 0, 'h12ab);

    // A permission fault must report a nonidentity, byte-exact final GPA.
    fence_all(); launch('h1234, LOAD);
    reply('h4000, entry('h40000, GR)); reply('h40001000, entry('h80000, VW));
    reply('h4010, entry('hc0000, GR)); finish(NONE, 'h1234, LOAD, 56'hc0001234, 64'h80001234);
    launch('h12ab, STORE); finish(GUEST, 'h12ab, STORE, 0, 64'h800012ab);

    // Current SUM/privilege/VS MXR are checked, not the original fill access.
    fence_all(); privilege = 0; launch('h1234, LOAD); nested(GR, GR); finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);
    privilege = 1; launch('h1234, LOAD); finish(PAGE, 'h1234, LOAD);
    vs_sum = 1; launch('h1234, LOAD); finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);
    fence_all(); defaults(); vs_mxr = 1; launch('h1234, LOAD); nested('h49, GR); finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);
    vs_mxr = 0; launch('h1234, LOAD); while (!completed) tick();
    if (!probe_hit || !probe_fault) $fatal(1, "disjoint stage permissions authorized a probe");
    finish(PAGE, 'h1234, LOAD);
    // HS MXR changes revalidate implicit PTE permissions with a walk.
    hs_mxr = 1; launch('h1234, LOAD); nested('h49, GR); finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);
    hs_mxr = 0; vs_mxr = 1; launch('h1234, LOAD); finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);

    // Root and environment-mode changes cannot hit another context's mapping.
    fence_all(); defaults(); launch('h1234, LOAD); nested(); finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);
    vs_root = 2; launch('h1234, LOAD); nested(VW, GW, 'h2000); finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);
    g_root = 8; launch('h1234, LOAD); reply('h8000, 0); finish(GUEST, 'h1234, LOAD, 0, 'h2000, 1);
    defaults(); vs_pbmte = 1; launch('h1234, LOAD); nested(); finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);
    vs_mode = 0; g_mode = 0; launch('h1234, LOAD); finish(NONE, 'h1234, LOAD, 'h1234, 'h1234);
    launch('h12ab, LOAD); finish(NONE, 'h12ab, LOAD, 'h12ab, 'h12ab);

    // Separate leaf memory types must survive both fill and lookup.
    fence_all(); defaults(); vs_pbmte = 1; g_pbmte = 1; launch('h1234, LOAD);
    nested(VW | (64'h1 << 61), GW | (64'h2 << 61));
    finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);
    launch('h12ab, LOAD); while (!completed) tick();
    if (vs_leaf[62:61] != 1 || g_leaf[62:61] != 2) $fatal(1, "cached PBMT lost");
    if (!probe_hit || probe_fault || probe_pbmt != 1) $fatal(1, "VS probe PBMT must override G");
    finish(NONE, 'h12ab, LOAD, 'h400012ab, 'h12ab);
    fence_all(); launch('h1234, LOAD); nested(VW, GW | (64'h2 << 61));
    finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);
    launch('h12ab, LOAD); while (!completed) tick();
    if (!probe_hit || probe_fault || probe_pbmt != 2) $fatal(1, "PMA VS leaf must inherit G PBMT");
    finish(NONE, 'h12ab, LOAD, 'h400012ab, 'h12ab);

    // Faults never populate the cache; implicit reads retain original class.
    fence_all(); defaults();
    for (int kind = 0; kind < 4; kind++) begin
      launch('h1234, kind[1:0]); reply('h4000, 0);
      finish(GUEST, 'h1234, kind[1:0], 0, 'h1000, 1);
    end
    launch('h1234, STORE); nested(); finish(NONE, 'h1234, STORE, 'h40001234, 'h1234);
    fence_all(); launch('h1234, STORE); wait_read('h4000);
    memory_ready = 1; memory_fault = 1; tick(); falling(); memory_ready = 0; memory_fault = 0;
    finish(PHYSICAL, 'h1234, STORE);
    launch('h1234, LOAD); nested(); finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);

    // Invalidation also wins on the actual walker-completion/refill edge.
    fence_all(); launch('h1234, LOAD); nested(); fence_all();
    if (completed) $fatal(1, "fenced refill completion escaped");
    launch('h1234, LOAD); nested(); finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);

    // Held TLB-hit results capture context before the caller changes its input.
    launch('h123a, LOAD); address = 'habc; vs_root = 99; access = STORE;
    finish(NONE, 'h123a, LOAD, 'h4000123a, 'h123a);
    defaults(); launch('h1234, LOAD); while (!completed) tick(); falling(); fence_all();
    if (completed || !command_ready) $fatal(1, "fence did not cancel held result");
    launch('h1234, LOAD); nested(); finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);

    // Invalidation on the final PTE edge must not allow a stale refill.
    fence_all(); launch('h1234, LOAD);
    reply('h4000, entry('h40000, GR)); reply('h40001000, entry(0, VW));
    wait_read('h4000); memory_ready = 1; tick(); falling(); memory_ready = 0;
    response_valid = 1; pte = entry('h40000, GW); fence_all(); response_valid = 0;
    if (completed) $fatal(1, "fenced response escaped");
    launch('h1234, LOAD); nested(); finish(NONE, 'h1234, LOAD, 'h40001234, 'h1234);

    // Fence during a delayed accepted PTE blocks reuse until the orphan drains.
    fence_all(); launch('h1234, LOAD); wait_read('h4000);
    memory_ready = 1; tick(); falling(); memory_ready = 0; fence_all();
    command_valid = 1; address = 'h5678;
    repeat (3) begin tick(); if (command_ready || memory_valid || completed) $fatal(1, "orphan owner lost"); end
    falling(); fence_all();
    if (command_ready) $fatal(1, "second fence forgot orphan");
    response_valid = 1; pte = entry('h40000, GW); tick(); falling(); response_valid = 0; command_valid = 0;
    launch('h5678, LOAD); nested(); finish(NONE, 'h5678, LOAD, 'h40005678, 'h5678);

    // Cancel is not a fence: a preexisting successful translation survives.
    launch('h5678, LOAD); cancel = 1; tick(); falling(); cancel = 0; #1;
    if (completed) $fatal(1, "canceled result escaped");
    launch('h5679, LOAD); finish(NONE, 'h5679, LOAD, 'h40005679, 'h5679);
    // Reset clears translation storage as well as request ownership.
    reset = 1; tick(); falling(); reset = 0;
    launch('h5678, LOAD); nested(); finish(NONE, 'h5678, LOAD, 'h40005678, 'h5678);
    $display("guest translation PASS (%0d checked transfers/completions)", checks);
    $finish;
  end
  initial begin #200000; $fatal(1, "guest translation watchdog"); end
endmodule
