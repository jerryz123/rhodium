// Checks compact 64-KiB translations, invalid N encodings, permissions, and TLB lifetime.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_svnapot_tb;
  logic clock = 0, reset = 1;
  always #5 clock = ~clock;
  logic command_valid = 0, command_ready, cancel = 0, pbmte = 1;
  logic [63:0] address = 64'h10000, lookup_address = 64'h10000;
  logic [1:0] access = 1, privilege = 1, validation_level = 0;
  logic sum = 0, mxr = 0;
  logic memory_ready = 0, memory_fault = 0, response_valid = 0, memory_valid;
  logic [63:0] pte = 0, memory_address;
  logic completion_ready = 0, completed, fault, access_fault;
  logic [55:0] result_address, tlb_address, probe_address;
  logic [43:0] result_base_ppn;
  logic [1:0] result_size, result_pbmt, tlb_size, tlb_pbmt, probe_pbmt;
  logic enabled = 1, invalidate = 0, hit, tlb_fault, probe_hit, probe_fault;
  logic pte_valid, disabled_pte_valid;
  int request_count = 0;
  localparam logic [63:0] N = 64'h8000000000000000;
  localparam logic [63:0] VA = 64'h12340000;
  localparam logic [55:0] PA = 56'h80000000;
  localparam logic [43:0] PPN = 44'h80000;
  RV5StageSvnapotFixture dut (.*);
  always @(posedge clock) if (!reset && memory_valid && memory_ready) request_count++;

  task automatic tick;
    @(posedge clock); #1;
  endtask

  task automatic clear_tlb;
    @(negedge clock); invalidate = 1;
    tick(); invalidate = 0; #1;
    assert (!hit && !probe_hit) else $fatal(1, "invalidation retained mapping");
  endtask

  task automatic start_walk(input logic [63:0] va);
    @(negedge clock); address = va; command_valid = 1;
    #1; assert (command_ready) else $fatal(1, "walker occupied");
    tick(); command_valid = 0;
  endtask

  task automatic supply(input logic [63:0] expected_address, value);
    #1;
    assert (memory_valid && memory_address == expected_address)
      else $fatal(1, "PTE request %h expected %h", memory_address, expected_address);
    repeat (2) begin
      tick();
      assert (memory_valid && memory_address == expected_address) else $fatal(1, "stalled request changed");
    end
    @(negedge clock); memory_ready = 1;
    tick(); memory_ready = 0;
    repeat (2) tick();
    @(negedge clock); pte = value; response_valid = 1;
    tick(); response_valid = 0;
  endtask

  task automatic walk(input logic [63:0] va, input int level, input logic [63:0] leaf);
    start_walk(va);
    supply(64'h1000 + ((va >> 30) & 511) * 8, level == 2 ? leaf : 64'h801);
    if (level < 2) supply(64'h2000 + ((va >> 21) & 511) * 8, level == 1 ? leaf : 64'hc01);
    if (level < 1) supply(64'h3000 + ((va >> 12) & 511) * 8, leaf);
  endtask

  task automatic finish(input bit bad, input logic [55:0] pa = 0,
                        input logic [1:0] size = 1, kind = 0,
                        input logic [43:0] base = PPN);
    assert (completed && fault == bad && !access_fault) else $fatal(1, "wrong walk status");
    repeat (2) begin
      if (!bad) assert (result_address == pa && result_size == size && result_base_ppn == base && result_pbmt == kind)
        else $fatal(1, "wrong normalized mapping: pa=%h base=%h size=%d", result_address, result_base_ppn, result_size);
      tick();
      assert (completed) else $fatal(1, "completion dropped under backpressure");
    end
    @(negedge clock); completion_ready = 1;
    tick(); completion_ready = 0;
  endtask

  task automatic lookup(input logic [63:0] va, input logic [55:0] pa,
                        input logic [1:0] size = 1, kind = 0);
    lookup_address = va; #1;
    assert (hit && !tlb_fault && probe_hit && !probe_fault && tlb_address == pa && probe_address == pa &&
            tlb_size == size && tlb_pbmt == kind && probe_pbmt == kind)
      else $fatal(1, "demand/probe mismatch va=%h pa=%h got=%h", va, pa, tlb_address);
  endtask

  initial begin
    repeat (2) tick(); reset = 0;
    // Architectural N encoding and leaf legality, independent of the implementation enum.
    for (int level = 0; level < 3; level++)
      for (int lo = 0; lo < 16; lo++)
        for (int flags = 0; flags < 16; flags++) begin
          validation_level = 2'(level);
          pte = N | (64'(lo) << 10) | 64'(flags); #1;
          assert (pte_valid == (level == 0 && lo == 8 && (flags & 1) != 0 &&
                  ((flags & 2) != 0 || (flags & 8) != 0) && !((flags & 4) != 0 && (flags & 2) == 0)))
            else $fatal(1, "NAPOT validation level=%d ppn-low=%h flags=%h", level, lo, flags);
          assert (!disabled_pte_valid) else $fatal(1, "disabled Svnapot accepted N");
        end
    // Start a cold walk in each subpage; every result covers all sixteen subpages.
    for (int first = 0; first < 16; first++) begin
      automatic int before_requests = request_count;
      clear_tlb();
      walk(VA + 64'(first * 4096 + 'h123), 0, N | (64'(PPN + 8) << 10) | (64'(first % 3) << 61) | 64'hcf);
      finish(0, PA + 56'(first * 4096 + 'h123), 1, 2'(first % 3));
      for (int page = 0; page < 16; page++)
        for (int offset_index = 0; offset_index < 3; offset_index++) begin
          automatic int offset = offset_index == 0 ? 0 : offset_index == 1 ? 'h123 : 'hfff;
          lookup(VA + 64'(page * 4096 + offset), PA + 56'(page * 4096 + offset), 1, 2'(first % 3));
        end
      assert (request_count == before_requests + 3) else $fatal(1, "NAPOT scanned more than one leaf");
      lookup_address = VA - 1; #1; assert (!hit && !probe_hit) else $fatal(1, "mapping extended below region");
      lookup_address = VA + 65536; #1; assert (!hit && !probe_hit) else $fatal(1, "mapping extended above region");
    end
    // Reserved encodings and non-leaf/upper-level N must fault through the real walker.
    for (int lo = 0; lo < 16; lo++) if (lo != 8) begin
      clear_tlb(); walk(VA, 0, N | (64'(PPN + 44'(lo)) << 10) | 64'hcf); finish(1);
      lookup_address = VA; #1; assert (!hit) else $fatal(1, "fault filled TLB");
    end
    for (int level = 0; level < 3; level++) begin
      clear_tlb(); walk(VA, level, N | (64'(PPN + 8) << 10) | 64'h1); finish(1);
      if (level != 0) begin clear_tlb(); walk(VA, level, N | (64'(PPN + 8) << 10) | 64'hcf); finish(1); end
    end
    for (int bit_index = 54; bit_index < 61; bit_index++) begin
      clear_tlb(); walk(VA, 0, N | (64'(PPN + 8) << 10) | (64'h1 << bit_index) | 64'hcf); finish(1);
    end
    // A/D are checked, never synthesized from neighboring PTEs or written back.
    clear_tlb(); walk(VA, 0, N | (64'(PPN + 8) << 10) | 64'h8f); finish(1);
    access = 2;
    clear_tlb(); walk(VA, 0, N | (64'(PPN + 8) << 10) | 64'h4f); finish(1);
    access = 1;
    clear_tlb(); walk(VA, 0, N | (64'(PPN + 8) << 10) | 64'h43); finish(0, PA);
    lookup(VA + 'hffff, PA + 'hffff);
    access = 2; #1; assert (hit && tlb_fault && probe_hit && !probe_fault) else $fatal(1, "store bypassed W/D");
    access = 0; #1; assert (hit && tlb_fault) else $fatal(1, "fetch bypassed X");
    privilege = 0; access = 1; #1; assert (hit && tlb_fault && probe_fault) else $fatal(1, "U bypassed permission");
    privilege = 1;
    clear_tlb(); access = 0; walk(VA, 0, N | (64'(PPN + 8) << 10) | 64'h59); finish(1);
    privilege = 0; walk(VA, 0, N | (64'(PPN + 8) << 10) | 64'h59); finish(0, PA);
    access = 1; mxr = 1; lookup(VA, PA);
    mxr = 0; #1; assert (hit && tlb_fault) else $fatal(1, "load bypassed MXR");
    privilege = 1; mxr = 1; sum = 0; #1; assert (hit && tlb_fault) else $fatal(1, "S bypassed SUM");
    sum = 1; lookup(VA, PA);
    privilege = 1; mxr = 0; sum = 0; access = 1;

    // Ordinary mapping geometry remains unchanged, including normalized superpage PPNs.
    for (int level = 0; level < 3; level++) begin
      automatic logic [55:0] offset = 56'(VA + 64'h123) & ((56'h1 << (12 + 9 * level)) - 1);
      automatic logic [1:0] size = level == 0 ? 0 : level == 1 ? 2 : 3;
      clear_tlb(); walk(VA + 64'h123, level, (64'(PPN) << 10) | 64'hcf);
      finish(0, PA + offset, size); lookup(VA + 64'h123, PA + offset, size);
    end
    // Replacement remains bounded to two compact entries, not sixteen synthetic fills.
    clear_tlb();
    for (int region = 0; region < 3; region++) begin
      walk(VA + 64'(region * 65536), 0, N | (64'(PPN + 8) << 10) | 64'hcf); finish(0, PA);
    end
    lookup_address = VA; #1; assert (!hit) else $fatal(1, "replacement failed");
    lookup(VA + 65536 + 'hfff, PA + 'hfff); lookup(VA + 131072 + 'hf000, PA + 'hf000);
    enabled = 0; #1;
    assert (hit && probe_hit && tlb_address == lookup_address[55:0] && tlb_pbmt == 0 && tlb_size == 0)
      else $fatal(1, "Bare inherited mapping metadata");
    enabled = 1;
    clear_tlb(); walk(VA, 0, N | (64'(PPN + 8) << 10) | 64'hcf);
    @(negedge clock); completion_ready = 1; invalidate = 1;
    tick(); completion_ready = 0; invalidate = 0; lookup_address = VA; #1;
    assert (!hit) else $fatal(1, "fill beat invalidation");
    walk(VA, 0, N | (64'(PPN + 8) << 10) | 64'hcf);
    @(negedge clock); cancel = 1; completion_ready = 1;
    tick(); cancel = 0; completion_ready = 0; #1;
    assert (!hit && !completed) else $fatal(1, "canceled completion escaped");
    $display("Svnapot compact mappings, encodings, permissions and lifetime passed");
    $finish;
  end
  initial begin #100000; $fatal(1, "Svnapot timeout"); end
endmodule
