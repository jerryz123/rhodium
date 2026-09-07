// Verifies registered instruction retry, MMU data-port drain, backpressure, faults, and prefetches.
module rv5stage_mmu_replay_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic [63:0] address; } instruction_req_bits_t;
  typedef struct packed { logic valid; instruction_req_bits_t bits; } instruction_req_t;
  typedef struct packed { logic [31:0] word; logic page_fault; logic access_fault; } instruction_resp_bits_t;
  typedef struct packed { logic valid; instruction_resp_bits_t bits; } instruction_resp_t;
  typedef struct packed {
    logic flush;
    logic invalidate_all;
    instruction_req_t request;
    ready_t response;
  } instruction_in_t;
  typedef struct packed { ready_t request; instruction_resp_t response; } instruction_out_t;
  typedef struct packed {
    logic [63:0] address;
    logic cacheable;
    logic device;
  } physical_instruction_req_bits_t;
  typedef struct packed {
    logic valid;
    physical_instruction_req_bits_t bits;
  } physical_instruction_req_t;
  typedef struct packed {
    logic [63:0] address;
    logic [3:0] access;
    logic [3:0] atomic;
    logic [1:0] width;
    logic unsigned_0;
    logic [63:0] data;
    logic [1:0] destination;
    logic [4:0] rd;
    logic [1:0] floating_point_precision;
  } data_req_bits_t;
  typedef struct packed { logic valid; data_req_bits_t bits; } data_req_t;
  typedef struct packed {
    logic access_fault;
    logic [63:0] data;
    logic [1:0] destination;
    logic [4:0] rd;
    logic [1:0] floating_point_precision;
  } data_resp_bits_t;
  typedef struct packed { logic valid; data_resp_bits_t bits; } data_resp_t;
  typedef struct packed { logic [63:0] address; logic [1:0] operation; } prefetch_bits_t;
  typedef struct packed { logic valid; prefetch_bits_t bits; } prefetch_t;
  typedef struct packed { data_req_t request; } data_in_t;
  typedef struct packed {
    ready_t request;
    logic request_fault;
    logic request_access_fault;
    data_resp_t response;
    logic drained;
  } data_out_t;
  typedef struct packed {
    ready_t request;
    instruction_resp_t response;
  } instruction_memory_in_t;
  typedef struct packed {
    logic flush;
    logic invalidate_all;
    physical_instruction_req_t request;
    ready_t response;
  } instruction_memory_out_t;
  typedef struct packed {
    ready_t request;
    logic request_fault;
    logic request_access_fault;
    data_resp_t response;
    logic drained;
  } data_memory_in_t;
  typedef struct packed { data_req_t request; } data_memory_out_t;

  localparam logic [1:0] PRIVILEGE_U = 2'd0;
  localparam logic [1:0] PRIVILEGE_S = 2'd1;
  localparam logic [3:0] MEMORY_LOAD = 4'd1;
  localparam logic [1:0] MEMORY_DOUBLE = 2'd3;
  localparam logic [1:0] DATA_DESTINATION_INTEGER = 2'd1;
  localparam logic [63:0] VIRTUAL_ADDRESS = 64'h4000;
  localparam logic [63:0] FAULT_VIRTUAL_ADDRESS = 64'h8000;
  localparam logic [63:0] PHYSICAL_ADDRESS = 64'h8000;
  localparam logic [63:0] SATP_SV39_ROOT_1 = 64'h80000000_00000001;
  localparam logic [63:0] LEVEL_2_POINTER = 64'h801;
  localparam logic [63:0] LEVEL_1_POINTER = 64'hc01;
  localparam logic [63:0] LEVEL_0_LEAF = 64'h2047;

  logic clock = 1'b0;
  logic reset = 1'b1;
  instruction_in_t instruction_in;
  data_in_t data_in;
  typedef struct packed { logic valid; logic [63:0] bits; } lookup_t;
  lookup_t instruction_lookup_out;
  ready_t instruction_lookup_in = '{ready: 1'b1};
  lookup_t data_lookup_out;
  instruction_memory_in_t instruction_memory_in;
  data_memory_in_t data_memory_in;
  prefetch_t prefetch_in;
  logic [1:0] privilege;
  logic [63:0] mstatus;
  logic [63:0] satp;
  logic invalidate_all;
  logic instruction_flush;
  instruction_out_t instruction_out;
  data_out_t data_out;
  instruction_memory_out_t instruction_memory_out;
  data_memory_out_t data_memory_out;
  prefetch_t physical_prefetch_out;
  logic data_request_valid;
  logic instruction_request_valid = 1'b0;
  logic [63:0] instruction_address = 64'h5000;
  logic pte_response_valid;
  logic [63:0] pte_response_data;
  logic [1:0] pte_requests;
  logic translated_request_seen;
  logic page_fault_phase;
  logic zero_request = 1'b0;
  logic [3:0] management_operation = 0;
  logic page_fault_pte_seen;
  logic memory_ready = 1'b0;
  logic memory_idle = 1'b0;
  logic ordinary_response_valid = 1'b0;
  data_req_bits_t stalled_request;
  logic instruction_phase = 1'b0;
  logic instruction_blocked = 1'b0;
  logic instruction_return_valid = 1'b0;
  logic [31:0] instruction_return_word;
  integer instruction_requests_seen = 0;
  integer instruction_responses_seen = 0;
  logic instruction_translation_phase = 1'b0;
  integer instruction_pte_requests = 0;

  RV5StageMmu dut (.*);
  always #5 clock = ~clock;

  always_comb begin
    instruction_in = '0;
    instruction_in.flush = instruction_flush;
    instruction_in.request.valid = instruction_request_valid;
    instruction_in.request.bits.address = instruction_address;
    instruction_in.response.ready = 1'b1;
    data_in.request.valid = data_request_valid;
    data_in.request.bits.address = (page_fault_phase ? FAULT_VIRTUAL_ADDRESS : VIRTUAL_ADDRESS) + (zero_request || management_operation != 0 ? 64'd63 : 64'd0);
    data_in.request.bits.access = management_operation != 0 ? management_operation : zero_request ? 4'd6 : 4'(MEMORY_LOAD);
    data_in.request.bits.atomic = '0;
    data_in.request.bits.width = MEMORY_DOUBLE;
    data_in.request.bits.unsigned_0 = 1'b1;
    data_in.request.bits.data = '0;
    data_in.request.bits.destination = DATA_DESTINATION_INTEGER;
    data_in.request.bits.rd = 5'd7;
    data_in.request.bits.floating_point_precision = '0;
    instruction_memory_in = '0;
    instruction_memory_in.request.ready = !instruction_blocked;
    instruction_memory_in.response.valid = instruction_return_valid;
    instruction_memory_in.response.bits = '{word: instruction_return_word, page_fault: 1'b0, access_fault: 1'b0};
    data_memory_in.request.ready = memory_ready;
    data_memory_in.request_fault = 1'b0;
    data_memory_in.request_access_fault = 1'b0;
    data_memory_in.response.valid = pte_response_valid || ordinary_response_valid;
    data_memory_in.response.bits.access_fault = 0;
    data_memory_in.response.bits.data = ordinary_response_valid ? 64'hfeedface_12345678 : pte_response_data;
    data_memory_in.response.bits.destination = ordinary_response_valid ? DATA_DESTINATION_INTEGER : 2'd0;
    data_memory_in.response.bits.rd = ordinary_response_valid ? 5'd7 : 5'd0;
    data_memory_in.response.bits.floating_point_precision = '0;
    data_memory_in.drained = memory_idle && !pte_response_valid;
  end

  always_ff @(posedge clock) begin
    if (reset) begin
      pte_response_valid <= 1'b0;
      pte_response_data <= '0;
      pte_requests <= '0;
      translated_request_seen <= 1'b0;
      page_fault_pte_seen <= 1'b0;
    end else begin
      pte_response_valid <= 1'b0;
      if (pte_response_valid)
        assert (!data_out.response.valid)
          else $fatal(1, "page-table response leaked onto the core data path");
      assert (instruction_phase || !instruction_memory_out.request.valid)
        else $fatal(1, "data miss unexpectedly issued an instruction-memory request");
      if (data_memory_out.request.valid && data_memory_in.request.ready) begin
        assert (data_lookup_out.valid &&
                data_lookup_out.bits[11:0] == data_memory_out.request.bits.address[11:0])
          else $fatal(1, "physical data acceptance lost its paired VIPT lookup");
        if (!data_request_valid)
          assert (data_lookup_out.bits == data_memory_out.request.bits.address)
            else $fatal(1, "PTW read did not supply a physical lookup index");
        if (instruction_translation_phase) begin
          case (instruction_pte_requests)
            0, 4: begin
              assert (data_memory_out.request.bits.address == 64'h1000)
                else $fatal(1, "ITLB walk lost its root");
              pte_response_data <= LEVEL_2_POINTER;
            end
            1, 5: begin
              assert (data_memory_out.request.bits.address == 64'h2000)
                else $fatal(1, "ITLB walk lost its middle level");
              pte_response_data <= LEVEL_1_POINTER;
            end
            2: begin
              assert (data_memory_out.request.bits.address == 64'h3020)
                else $fatal(1, "ITLB walk used the live input instead of the retained PC");
              pte_response_data <= 64'h204b; // Valid, readable, executable, accessed.
            end
            3: begin
              assert (data_memory_out.request.bits.address == 64'h1000)
                else $fatal(1, "canceled ITLB fault used an unexpected root");
              pte_response_data <= 0;
            end
            6: begin
              assert (data_memory_out.request.bits.address == 64'h3028)
                else $fatal(1, "post-redirect ITLB walk lost its new PC");
              pte_response_data <= 64'h284b; // Maps VA 0x5000 to PA 0xa000.
            end
            default: $fatal(1, "ITLB hit unnecessarily restarted the walker");
          endcase
          pte_response_valid <= 1'b1;
          instruction_pte_requests <= instruction_pte_requests + 1;
        end else if (page_fault_phase) begin
          assert (!page_fault_pte_seen && data_memory_out.request.bits.address == 64'h1000)
            else $fatal(1, "faulting walk issued an unexpected PTE request");
          pte_response_valid <= 1'b1;
          pte_response_data <= 64'h0;
          page_fault_pte_seen <= 1'b1;
        end else if (pte_requests == 0) begin
          assert (data_memory_out.request.bits.address == 64'h1000)
            else $fatal(1, "level-2 PTE address was incorrect");
          pte_response_valid <= 1'b1;
          pte_response_data <= LEVEL_2_POINTER;
          pte_requests <= 1;
        end else if (pte_requests == 1) begin
          assert (data_memory_out.request.bits.address == 64'h2000)
            else $fatal(1, "level-1 PTE address was incorrect");
          pte_response_valid <= 1'b1;
          pte_response_data <= LEVEL_1_POINTER;
          pte_requests <= 2;
        end else if (pte_requests == 2) begin
          assert (data_memory_out.request.bits.address == 64'h3020)
            else $fatal(1, "level-0 PTE address was incorrect");
          pte_response_valid <= 1'b1;
          pte_response_data <= LEVEL_0_LEAF;
          pte_requests <= 3;
        end else begin
          assert (data_request_valid && data_memory_out.request.bits.address == PHYSICAL_ADDRESS + (management_operation != 0 ? 64'd63 : 64'd0) &&
                  data_memory_out.request.bits.destination == DATA_DESTINATION_INTEGER &&
                  data_memory_out.request.bits.rd == 5'd7)
            else $fatal(1, "replayed request was not translated with its metadata intact");
          translated_request_seen <= 1'b1;
        end
      end
    end
  end

  always_ff @(posedge clock) begin
    instruction_return_valid <= 1'b0;
    if (instruction_phase && !instruction_flush) begin
      if (instruction_memory_out.request.valid && instruction_memory_in.request.ready) begin
        assert (instruction_requests_seen < (instruction_translation_phase ? 5 : 2) &&
                instruction_memory_out.request.bits.address ==
                  (instruction_requests_seen == 4 ? 64'ha000 : 64'h8000 + 4 * 64'(instruction_requests_seen % 2)))
          else $fatal(1, "instruction retry lost order or duplicated a physical request");
        instruction_return_valid <= 1'b1;
        instruction_return_word <= 32'h100 + 32'(instruction_requests_seen);
        instruction_requests_seen <= instruction_requests_seen + 1;
      end
      if (instruction_out.response.valid) begin
        assert (instruction_out.response.bits.word == 32'h100 + 32'(instruction_responses_seen) &&
                !instruction_out.response.bits.page_fault && !instruction_out.response.bits.access_fault)
          else $fatal(1, "registered instruction response lost its owner");
        instruction_responses_seen <= instruction_responses_seen + 1;
      end
    end
  end

  initial begin
    wait (!reset);
    repeat (600) @(posedge clock);
    $fatal(1, "DTLB walk or replay did not complete");
  end

  task automatic tick;
    @(posedge clock);
    #1;
  endtask

  task automatic check_prefetch(input logic valid, input logic [63:0] address = 0,
                                input logic [1:0] operation = 0);
    assert (physical_prefetch_out.valid == valid)
      else $fatal(1, "prefetch valid mismatch: expected %b, got %b", valid, physical_prefetch_out.valid);
    if (valid) begin
      assert (physical_prefetch_out.bits.address == address &&
              physical_prefetch_out.bits.operation == operation)
        else $fatal(1, "prefetch address or operation was not preserved");
    end
  endtask

  task automatic check_isolated_hint(input logic [63:0] address,
                                     input logic [1:0] operation,
                                     input logic survives,
                                     input logic [63:0] physical_address = 0);
    @(negedge clock);
    prefetch_in = '{valid: 1'b1, bits: '{address: address, operation: operation}};
    #1;
    check_prefetch(0);
    tick();
    check_prefetch(0);
    @(negedge clock);
    prefetch_in = '0;
    tick();
    check_prefetch(survives, physical_address, operation);
    tick();
    check_prefetch(0);
    assert (!data_memory_out.request.valid)
      else $fatal(1, "prefetch claimed the page-table walker");
  endtask

  initial begin
    data_request_valid = 1'b0;
    page_fault_phase = 1'b0;
    prefetch_in = '0;
    privilege = PRIVILEGE_S;
    mstatus = '0;
    satp = SATP_SV39_ROOT_1;
    invalidate_all = 1'b0;
    instruction_flush = 1'b0;
    repeat (2) @(posedge clock);
    #1 reset = 1'b0;

    @(negedge clock);
    data_request_valid = 1'b1;
    #1;
    assert (!data_out.request.ready && !data_out.request_fault &&
            !data_out.request_access_fault && !data_memory_out.request.valid)
      else $fatal(1, "initial DTLB miss was not rejected cleanly");
    assert (data_lookup_out.valid && data_lookup_out.bits == VIRTUAL_ADDRESS)
      else $fatal(1, "DTLB miss suppressed the early virtual SRAM lookup");
    @(posedge clock);
    #1 data_request_valid = 1'b0;

    // A walk must drain older data work, including its nonbackpressured reply.
    repeat (2) begin
      tick();
      assert (!data_memory_out.request.valid && !data_out.drained)
        else $fatal(1, "page-table request bypassed older data work");
    end
    @(negedge clock);
    ordinary_response_valid = 1'b1;
    #1;
    assert (data_out.response.valid && data_out.response.bits == data_memory_in.response.bits)
      else $fatal(1, "older data response was lost or routed to the walker");
    tick();
    @(negedge clock);
    ordinary_response_valid = 1'b0;
    memory_idle = 1'b1;
    tick();
    assert (!data_memory_out.request.valid)
      else $fatal(1, "walker did not wait for two quiet observations");
    tick();
    assert (data_memory_out.request.valid && data_memory_out.request.bits.address == 64'h1000)
      else $fatal(1, "drained walker did not offer the first PTE request");
    stalled_request = data_memory_out.request.bits;
    repeat (3) begin
      tick();
      assert (data_memory_out.request.valid && data_memory_out.request.bits == stalled_request && pte_requests == 0)
        else $fatal(1, "stalled PTE request changed or was accepted without readiness");
    end
    @(negedge clock);
    memory_ready = 1'b1;

    wait (pte_requests == 3 && data_out.drained);
    @(negedge clock);
    memory_ready = 1'b0;
    data_request_valid = 1'b1;
    #1;
    stalled_request = data_memory_out.request.bits;
    repeat (2) begin
      assert (!data_out.request.ready && data_memory_out.request.valid &&
              data_memory_out.request.bits == stalled_request && !translated_request_seen)
        else $fatal(1, "translated request did not propagate downstream backpressure");
      tick();
    end
    @(negedge clock);
    memory_ready = 1'b1;
    #1;
    assert (data_out.request.ready && data_memory_out.request.valid &&
            data_memory_out.request.bits.address == PHYSICAL_ADDRESS)
      else $fatal(1, "replayed request did not hit the filled DTLB");
    assert (data_lookup_out.valid && data_lookup_out.bits == VIRTUAL_ADDRESS)
      else $fatal(1, "DTLB hit replaced the early virtual index with a physical address");
    @(posedge clock);
    #1 data_request_valid = 1'b0;
    assert (translated_request_seen)
      else $fatal(1, "translated replay was not accepted downstream");
    @(negedge clock);
    ordinary_response_valid = 1'b1;
    #1;
    assert (data_out.response.valid && data_out.response.bits == data_memory_in.response.bits)
      else $fatal(1, "replayed data response lost its payload or metadata");
    tick();
    @(negedge clock);
    ordinary_response_valid = 1'b0;

    // A writable but non-dirty leaf may serve loads, but CBO.ZERO must fault
    // under the core's fault-on-A/D policy, even on a nonaligned TLB hit.
    @(negedge clock);
    zero_request = 1'b1;
    data_request_valid = 1'b1;
    #1;
    assert (data_out.request.ready && data_out.request_fault && !data_memory_out.request.valid)
      else $fatal(1, "CBO.ZERO did not enforce store dirty-bit permission");
    @(posedge clock);
    #1;
    data_request_valid = 1'b0;
    zero_request = 1'b0;

    // Management uses a distinct translation class: A is required, D is not.
    // The original byte offset survives translation for precise trap metadata.
    for (int operation = 7; operation <= 9; operation++) begin
      @(negedge clock); management_operation = 4'(operation); data_request_valid = 1;
      #1;
      assert (data_out.request.ready && !data_out.request_fault && data_memory_out.request.valid && data_memory_out.request.bits.access == 4'(operation) && data_memory_out.request.bits.address == PHYSICAL_ADDRESS + 63)
        else $fatal(1, "CMO did not use management translation permissions");
      tick();
      @(negedge clock); data_request_valid = 0;
    end
    // MPRV affects translation even though xenvcfg authorization uses current M.
    privilege = 2'd3; mstatus = 64'h20000; data_request_valid = 1;
    #1;
    assert (data_out.request_fault && !data_memory_out.request.valid)
      else $fatal(1, "CMO ignored effective U privilege under MPRV");
    tick();
    @(negedge clock); data_request_valid = 0; management_operation = 0;
    privilege = PRIVILEGE_S; mstatus = 0;

    // The leaf is readable and accessed but not dirty. PREFETCH.W is still
    // permitted because prefetch translation accepts any R/W/X permission and
    // ignores A/D state.
    @(negedge clock);
    prefetch_in.valid = 1'b1;
    prefetch_in.bits.address = VIRTUAL_ADDRESS;
    prefetch_in.bits.operation = 2'd3;
    #1;
    check_prefetch(0);
    tick();
    check_prefetch(0);

    // Adjacent hints retain their own address/operation and emerge one per
    // cycle, but neither can bypass either register boundary.
    @(negedge clock);
    prefetch_in.bits.address = VIRTUAL_ADDRESS + 64'h7f;
    prefetch_in.bits.operation = 2'd2;
    tick();
    check_prefetch(1, PHYSICAL_ADDRESS, 2'd3);

    // A prefetch miss is dropped and must not claim the page-table walker.
    @(negedge clock);
    prefetch_in.valid = 1'b1;
    prefetch_in.bits.address = VIRTUAL_ADDRESS + 64'h1000;
    prefetch_in.bits.operation = 2'd2;
    #1;
    check_prefetch(1, PHYSICAL_ADDRESS, 2'd3);
    tick();
    check_prefetch(1, PHYSICAL_ADDRESS + 64'h40, 2'd2);
    @(negedge clock);
    prefetch_in = '0;
    repeat (4) begin
      tick();
      check_prefetch(0);
      assert (!data_memory_out.request.valid)
        else $fatal(1, "dropped prefetch later initiated a page-table walk");
    end

    // This address hits only DTLB: an instruction hint must not borrow it.
    check_isolated_hint(VIRTUAL_ADDRESS, 2'd1, 0);
    check_isolated_hint(VIRTUAL_ADDRESS, 2'd0, 0);
    @(negedge clock);
    privilege = PRIVILEGE_U;
    tick();
    check_isolated_hint(VIRTUAL_ADDRESS, 2'd2, 0);
    @(negedge clock);
    privilege = PRIVILEGE_S;
    tick();

    @(negedge clock);
    page_fault_phase = 1'b1;
    data_request_valid = 1'b1;
    #1;
    assert (!data_out.request.ready && !data_out.request_fault &&
            !data_out.request_access_fault && !data_memory_out.request.valid)
      else $fatal(1, "faulting DTLB miss was not rejected cleanly");
    @(posedge clock);
    #1 data_request_valid = 1'b0;

    wait (page_fault_pte_seen);
    repeat (2) @(posedge clock);

    // The translated entry is supervisor-only. A user-mode lookup therefore
    // faults directly in the DTLB, but must not consume the pending walker
    // fault for FAULT_VIRTUAL_ADDRESS.
    @(negedge clock);
    page_fault_phase = 1'b0;
    privilege = PRIVILEGE_U;
    data_request_valid = 1'b1;
    #1;
    assert (data_out.request.ready && data_out.request_fault &&
            !data_out.request_access_fault && !data_memory_out.request.valid)
      else $fatal(1, "cached permission fault was not accepted locally");
    @(posedge clock);
    #1 data_request_valid = 1'b0;
    assert (data_out.drained)
      else $fatal(1, "saved replay fault prevented architectural drain");

    @(negedge clock);
    page_fault_phase = 1'b1;
    privilege = PRIVILEGE_S;
    data_request_valid = 1'b1;
    #1;
    assert (data_out.request.ready && data_out.request_fault &&
            !data_out.request_access_fault && !data_memory_out.request.valid)
      else $fatal(1, "faulting replay did not consume the correlated page fault");
    @(posedge clock);
    #1 data_request_valid = 1'b0;
    assert (data_out.drained)
      else $fatal(1, "consumed page fault remained latched");

    // Bare hints keep the same latency and line alignment, including I hints.
    @(negedge clock);
    satp = '0;
    tick();
    check_isolated_hint(PHYSICAL_ADDRESS + 64'h7f, 2'd1, 1, PHYSICAL_ADDRESS + 64'h40);
    // The test physical map covers only the CHI physical-address width.
    check_isolated_hint(64'h80000000_00000000, 2'd2, 0);

    // Flush/reset and each relevant translation-context change cancel either
    // occupied stage at the edge. Already presented physical hints are not
    // withdrawn combinationally, so cancellation cannot reopen a demand path.
    for (int cancellation = 0; cancellation < 7; cancellation++) begin
      for (int stage = 1; stage <= 2; stage++) begin
        @(negedge clock);
        prefetch_in = '{valid: 1'b1, bits: '{address: PHYSICAL_ADDRESS, operation: 2'd2}};
        tick();
        check_prefetch(0);
        if (stage == 2) begin
          @(negedge clock);
          prefetch_in = '0;
          tick();
          check_prefetch(1, PHYSICAL_ADDRESS, 2'd2);
        end
        @(negedge clock);
        // Leave ingress valid while canceling the first stage: new hints on
        // the cancellation edge must also be discarded.
        case (cancellation)
          0: instruction_flush = 1;
          1: invalidate_all = 1;
          2: satp = satp ^ 64'd1;
          3: privilege = privilege == PRIVILEGE_S ? PRIVILEGE_U : PRIVILEGE_S;
          4: mstatus = mstatus ^ (64'd1 << 18);
          5: mstatus = mstatus ^ (64'd1 << 19);
          6: reset = 1;
        endcase
        #1;
        check_prefetch(stage == 2, PHYSICAL_ADDRESS, 2'd2);
        tick();
        check_prefetch(0);
        @(negedge clock);
        prefetch_in = '0;
        instruction_flush = 0;
        invalidate_all = 0;
        reset = 0;
        repeat (3) begin
          tick();
          check_prefetch(0);
          assert (!data_memory_out.request.valid && !instruction_memory_out.request.valid)
            else $fatal(1, "canceled hint produced demand or page-table traffic");
        end
      end
    end
    check_isolated_hint(PHYSICAL_ADDRESS, 2'd3, 1, PHYSICAL_ADDRESS);
    // ITLB miss and physical execute denial must not suppress the early read.
    @(negedge clock);
    satp = SATP_SV39_ROOT_1;
    privilege = PRIVILEGE_S;
    instruction_request_valid = 1;
    #1;
    assert (instruction_lookup_out.valid && instruction_lookup_out.bits == instruction_address &&
            instruction_out.request.ready && !instruction_memory_out.request.valid)
      else $fatal(1, "ITLB miss waited for translation before presenting its index");
    instruction_flush = 1;
    #1;
    assert (!instruction_lookup_out.valid)
      else $fatal(1, "flushed fetch still presented a live virtual lookup");
    instruction_request_valid = 0;
    tick();
    @(negedge clock);
    instruction_flush = 0;
    satp = 0;
    instruction_address = 64'h80000000_00000000;
    instruction_request_valid = 1;
    #1;
    assert (instruction_lookup_out.valid && instruction_out.request.ready &&
            !instruction_memory_out.request.valid)
      else $fatal(1, "PMA-denied fetch did not separate early read from physical resolution");
    tick();
    instruction_request_valid = 0;
    // Translation and fault classification use the admitted S1 address, not
    // the live S0 payload, and publish the fault through S2 ownership.
    instruction_address = 64'h80000000;
    tick();
    assert (instruction_out.response.valid && instruction_out.response.bits.access_fault)
      else $fatal(1, "PMA-denied early fetch lost its architectural fault");
    tick();

    // Admit consecutive S0 words while S1 is blocked, then alter the live
    // request payload. Local rereads must resolve each original word once.
    @(negedge clock);
    instruction_phase = 1;
    instruction_blocked = 1;
    instruction_address = 64'h8000;
    instruction_request_valid = 1;
    #1;
    assert (instruction_out.request.ready && !instruction_memory_out.request.valid)
      else $fatal(1, "S0 instruction admission waited for physical readiness");
    tick();
    instruction_address = 64'h8004;
    #1;
    assert (instruction_out.request.ready && instruction_memory_out.request.valid &&
            instruction_memory_out.request.bits.address == 64'h8000)
      else $fatal(1, "S1 translation used the live S0 address");
    tick();
    instruction_request_valid = 0;
    instruction_address = 64'hdead0000;
    repeat (4) tick();
    assert (instruction_lookup_out.valid && instruction_lookup_out.bits == 64'h8000)
      else $fatal(1, "blocked instruction did not retry its retained virtual address");
    instruction_blocked = 0;
    wait (instruction_responses_seen == 2);
    repeat (3) tick();
    assert (instruction_requests_seen == 2 && !instruction_memory_out.request.valid)
      else $fatal(1, "instruction retry duplicated completion");

    @(negedge clock);
    instruction_blocked = 1;
    instruction_request_valid = 1;
    instruction_address = 64'h8008;
    tick();
    instruction_request_valid = 0;
    instruction_flush = 1;
    tick();
    instruction_flush = 0;
    instruction_blocked = 0;
    repeat (4) tick();
    assert (instruction_requests_seen == 2 && !instruction_lookup_out.valid)
      else $fatal(1, "flushed S1 instruction escaped to physical memory");

    // A real ITLB miss keeps two admitted PCs across the entire walk. The
    // second word must reuse the fill, with no duplicate physical acceptance.
    @(negedge clock);
    instruction_translation_phase = 1;
    satp = SATP_SV39_ROOT_1;
    privilege = PRIVILEGE_S;
    mstatus = 0;
    instruction_address = VIRTUAL_ADDRESS;
    instruction_request_valid = 1;
    #1;
    assert (instruction_out.request.ready && !instruction_memory_out.request.valid)
      else $fatal(1, "ITLB miss prevented structural S0 admission");
    tick();
    instruction_address = VIRTUAL_ADDRESS + 4;
    #1;
    assert (instruction_out.request.ready && !instruction_memory_out.request.valid)
      else $fatal(1, "younger S0 request borrowed an unresolved translation");
    tick();
    instruction_request_valid = 0;
    instruction_address = 64'hdead0000;
    wait (instruction_responses_seen == 4);
    repeat (4) tick();
    assert (instruction_pte_requests == 3 && instruction_requests_seen == 4 &&
            !instruction_lookup_out.valid && !instruction_memory_out.request.valid)
      else $fatal(1, "ITLB local replay did not finish exactly once per admitted word");

    // Retain a walker fault while the reread port is unavailable, then redirect.
    // The canceled fault must not strand the walker for a subsequent ITLB miss.
    @(negedge clock);
    instruction_address = 64'h9000;
    instruction_request_valid = 1;
    tick();
    instruction_request_valid = 0;
    instruction_lookup_in.ready = 0;
    wait (instruction_pte_requests == 4);
    repeat (8) tick();
    assert (!instruction_out.response.valid && instruction_requests_seen == 4)
      else $fatal(1, "blocked fault escaped before its retained S1 request resolved");
    instruction_flush = 1;
    tick();
    instruction_flush = 0;
    instruction_lookup_in.ready = 1;
    instruction_address = 64'h5000;
    instruction_request_valid = 1;
    tick();
    instruction_request_valid = 0;
    instruction_address = 64'hdead0000;
    wait (instruction_responses_seen == 5);
    repeat (4) tick();
    assert (instruction_pte_requests == 7 && instruction_requests_seen == 5)
      else $fatal(1, "redirect did not release the retained ITLB fault");
    $display("RV5Stage registered ITLB retry, DTLB demand, faults, and pipelined prefetch translation passed");
    $finish;
  end
endmodule
