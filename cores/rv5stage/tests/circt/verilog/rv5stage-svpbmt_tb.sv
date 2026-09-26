// Checks Svpbmt encoding, walk capture, TLB lifetime, and PMA-independent attribute resolution.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_svpbmt_tb;
  logic clock = 0, reset = 1;
  always #5 clock = ~clock;
  logic command_valid = 0, command_ready, pbmte = 0, cancel = 0;
  logic [63:0] address = 64'h4000, lookup_address = 64'h4123;
  logic [1:0] access = 1;
  logic memory_ready = 0, memory_fault = 0, response_valid = 0, memory_valid;
  logic [63:0] pte = 0, memory_address;
  logic completion_ready = 0, completed, fault, access_fault;
  logic [1:0] result_pbmt, tlb_pbmt, probe_pbmt;
  logic [55:0] result_address, tlb_address;
  logic enabled = 1, invalidate = 0, hit, tlb_fault, probe_hit;
  typedef struct packed {
    logic mapped, readable, writable, executable, cacheable, atomic_capable;
    logic device, read_idempotent, cache_block_zero, instruction_cacheable;
  } physical_t;
  typedef struct packed {
    logic cacheable, instruction_cacheable, read_idempotent, strongly_ordered;
    logic fence_memory, fence_io;
  } attributes_t;
  physical_t physical;
  attributes_t attributes;
  logic [1:0] attribute_pbmt = 0;
  logic pte_valid;
  logic [63:0] envcfg = 0, envcfg_fields, disabled_fields;
  logic [31:0] rv32_fields;
  RV5StageSvpbmtFixture dut (.*);

  task automatic tick;
    @(posedge clock); #1;
  endtask

  function automatic logic [63:0] leaf(input int kind, input logic [43:0] ppn);
    return (64'(kind) << 61) | (64'(ppn) << 10) | 64'hcf;
  endfunction

  task automatic clear_tlb;
    @(negedge clock); invalidate = 1;
    tick(); invalidate = 0;
    #1;
    assert (!hit && !probe_hit) else $fatal(1, "invalidation retained a translation");
  endtask

  task automatic start_walk(input bit enable_pbmt);
    @(negedge clock);
    command_valid = 1;
    pbmte = enable_pbmt;
    #1;
    assert (command_ready) else $fatal(1, "walker not idle");
    tick();
    command_valid = 0;
    // A walk owns its accepted PBMTE, not the live request's next value.
    pbmte = !enable_pbmt;
  endtask

  task automatic supply_pte(input logic [63:0] expected_address, input logic [63:0] value);
    int waited;
    waited = 0;
    while (!memory_valid && waited < 10) begin tick(); waited++; end
    assert (memory_valid && memory_address == expected_address)
      else $fatal(1, "PTE request got %h expected %h", memory_address, expected_address);
    repeat (2) begin
      tick();
      assert (memory_valid && memory_address == expected_address)
        else $fatal(1, "stalled PTE request changed");
    end
    @(negedge clock); memory_ready = 1;
    tick(); memory_ready = 0;
    @(negedge clock); pte = value; response_valid = 1;
    tick(); response_valid = 0;
  endtask

  task automatic finish_walk(input bit expect_fault, input bit expect_access_fault,
                              input int kind, input logic [55:0] expected_address);
    assert (completed && fault == expect_fault && access_fault == expect_access_fault)
      else $fatal(1, "walk completion/fault mismatch");
    assert (result_pbmt == 2'(kind)) else $fatal(1, "walk lost PBMT");
    if (!expect_fault && !expect_access_fault)
      assert (result_address == expected_address) else $fatal(1, "walk address mismatch");
    repeat (2) begin
      tick();
      assert (completed && result_pbmt == 2'(kind)) else $fatal(1, "held completion lost PBMT");
      assert (!hit) else $fatal(1, "unaccepted completion filled TLB");
    end
    @(negedge clock); completion_ready = 1;
    tick(); completion_ready = 0;
    #1;
    assert (hit == !(expect_fault || expect_access_fault)) else $fatal(1, "TLB filled fault or lost success");
    if (!expect_fault && !expect_access_fault) begin
      assert (!tlb_fault && probe_hit && tlb_pbmt == 2'(kind) && probe_pbmt == 2'(kind))
        else $fatal(1, "demand/probe PBMT mismatch");
      assert (tlb_address == expected_address + 56'h123) else $fatal(1, "TLB address mismatch");
      enabled = 0; #1;
      assert (hit && probe_hit && tlb_pbmt == 0 && probe_pbmt == 0 && tlb_address == lookup_address[55:0])
        else $fatal(1, "Bare inherited stale PBMT");
      enabled = 1;
    end
  endtask

  initial begin
    physical = '0;
    repeat (2) tick(); reset = 0;
    // Pure encoding checks include pointer PTEs, disabled PBMTE and reserved bits.
    for (int en = 0; en < 2; en++)
      for (int kind = 0; kind < 4; kind++)
        for (int is_leaf = 0; is_leaf < 2; is_leaf++) begin
          pbmte = 1'(en);
          pte = (64'(kind) << 61) | (is_leaf != 0 ? 64'hcf : 64'h1);
          #1;
          assert (pte_valid == (kind == 0 || (en != 0 && is_leaf != 0 && kind < 3)))
            else $fatal(1, "PBMT structural validation mismatch");
          for (int bit_index = 54; bit_index < 64; bit_index++)
            if (bit_index != 61 && bit_index != 62) begin
              pte[bit_index] = 1; #1;
              assert (!pte_valid) else $fatal(1, "reserved/NAPOT bit accepted");
              pte[bit_index] = 0;
            end
        end
    envcfg = '1; #1;
    assert (envcfg_fields == 64'h4000000000000000 && disabled_fields == 0 && rv32_fields == 0)
      else $fatal(1, "PBMTE WARL fields incorrect");
    for (int mapped = 0; mapped < 2; mapped++)
      for (int device = 0; device < 2; device++)
        for (int kind = 0; kind < 3; kind++) begin
          physical = '1;
          physical.mapped = 1'(mapped);
          physical.device = 1'(device);
          physical.cacheable = device == 0;
          physical.instruction_cacheable = device == 0;
          physical.read_idempotent = device == 0;
          attribute_pbmt = 2'(kind); #1;
          assert (attributes.cacheable == (mapped != 0 && kind == 0 && device == 0) &&
                  attributes.instruction_cacheable == (mapped != 0 && kind == 0 && device == 0) &&
                  attributes.read_idempotent == (mapped != 0 && (kind == 1 || (kind == 0 && device == 0))) &&
                  attributes.strongly_ordered == (mapped != 0 && (kind == 2 || (kind == 0 && device != 0))) &&
                  attributes.fence_memory == (mapped != 0 && (kind == 1 || device == 0)) &&
                  attributes.fence_io == (mapped != 0 && (kind == 2 || device != 0)))
            else $fatal(1, "effective attributes mismatch");
        end

    // Read-only boot ROM can permit instruction-only caching under its PMA.
    physical = '0;
    physical.mapped = 1; physical.readable = 1; physical.executable = 1;
    physical.instruction_cacheable = 1; physical.read_idempotent = 1;
    attribute_pbmt = 0; #1;
    assert (!attributes.cacheable && attributes.instruction_cacheable)
      else $fatal(1, "PMA lost instruction-only caching");
    attribute_pbmt = 1; #1;
    assert (!attributes.cacheable && !attributes.instruction_cacheable)
      else $fatal(1, "NC retained instruction-only caching");

    for (int kind = 0; kind < 3; kind++) begin
      clear_tlb(); start_walk(1);
      supply_pte(64'h1000, 64'h801);
      supply_pte(64'h2000, 64'hc01);
      supply_pte(64'h3020, leaf(kind, 44'h8));
      finish_walk(0, 0, kind, 56'h8000);
    end
    // Every supported leaf level retains PBMT and its own address geometry.
    clear_tlb(); start_walk(1);
    supply_pte(64'h1000, 64'h801);
    supply_pte(64'h2000, leaf(1, 44'h200));
    finish_walk(0, 0, 1, 56'h204000);
    clear_tlb(); start_walk(1);
    supply_pte(64'h1000, leaf(2, 44'h40000));
    finish_walk(0, 0, 2, 56'h40004000);

    for (int kind = 1; kind < 4; kind++) begin
      clear_tlb(); start_walk(0);
      supply_pte(64'h1000, leaf(kind, 44'h40000));
      finish_walk(1, 0, 0, 0);
      clear_tlb(); start_walk(1);
      supply_pte(64'h1000, (64'(kind) << 61) | 64'h801);
      finish_walk(1, 0, 0, 0);
    end
    clear_tlb(); start_walk(1);
    supply_pte(64'h1000, leaf(3, 44'h40000));
    finish_walk(1, 0, 0, 0);
    clear_tlb(); start_walk(1);
    @(negedge clock); memory_ready = 1; memory_fault = 1;
    tick(); memory_ready = 0; memory_fault = 0;
    finish_walk(0, 1, 0, 0);

    // Architectural invalidation wins over a simultaneous successful fill.
    clear_tlb(); start_walk(1);
    supply_pte(64'h1000, leaf(1, 44'h40000));
    @(negedge clock); invalidate = 1; completion_ready = 1;
    tick(); invalidate = 0; completion_ready = 0; #1;
    assert (!hit && !probe_hit) else $fatal(1, "fill beat invalidation");
    // Cancel a retained result without publishing it to translation consumers.
    start_walk(1); supply_pte(64'h1000, leaf(2, 44'h40000));
    @(negedge clock); cancel = 1; completion_ready = 1;
    tick(); cancel = 0; completion_ready = 0; #1;
    assert (!completed && !hit) else $fatal(1, "cancelled result escaped");
    $display("Svpbmt PTE, attributes, walker and TLB checks passed");
    $finish;
  end
  initial begin #20000; $fatal(1, "Svpbmt fixture timed out"); end
endmodule
