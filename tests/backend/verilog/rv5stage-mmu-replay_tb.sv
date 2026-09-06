// Verifies DTLB replay and preserves walker faults across unrelated permission faults.
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
    logic [2:0] access;
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
    logic [63:0] data;
    logic [1:0] destination;
    logic [4:0] rd;
    logic [1:0] floating_point_precision;
  } data_resp_bits_t;
  typedef struct packed { logic valid; data_resp_bits_t bits; } data_resp_t;
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
  localparam logic [2:0] MEMORY_LOAD = 3'd1;
  localparam logic [1:0] MEMORY_DOUBLE = 2'd3;
  localparam logic [1:0] DATA_DESTINATION_INTEGER = 2'd1;
  localparam logic [63:0] VIRTUAL_ADDRESS = 64'h4000;
  localparam logic [63:0] FAULT_VIRTUAL_ADDRESS = 64'h8000;
  localparam logic [63:0] PHYSICAL_ADDRESS = 64'h8000;
  localparam logic [63:0] SATP_SV39_ROOT_1 = 64'h80000000_00000001;
  localparam logic [63:0] LEVEL_2_POINTER = 64'h801;
  localparam logic [63:0] LEVEL_1_POINTER = 64'hc01;
  localparam logic [63:0] LEVEL_0_LEAF = 64'h2043;

  logic clock = 1'b0;
  logic reset = 1'b1;
  instruction_in_t instruction_in;
  data_in_t data_in;
  instruction_memory_in_t instruction_memory_in;
  data_memory_in_t data_memory_in;
  logic [1:0] privilege;
  logic [63:0] mstatus;
  logic [63:0] satp;
  logic invalidate_all;
  instruction_out_t instruction_out;
  data_out_t data_out;
  instruction_memory_out_t instruction_memory_out;
  data_memory_out_t data_memory_out;
  logic data_request_valid;
  logic pte_response_valid;
  logic [63:0] pte_response_data;
  logic [1:0] pte_requests;
  logic translated_request_seen;
  logic page_fault_phase;
  logic page_fault_pte_seen;

  RV5StageMmu dut (.*);
  always #5 clock = ~clock;

  always_comb begin
    instruction_in = '0;
    instruction_in.response.ready = 1'b1;
    data_in.request.valid = data_request_valid;
    data_in.request.bits.address = page_fault_phase ? FAULT_VIRTUAL_ADDRESS : VIRTUAL_ADDRESS;
    data_in.request.bits.access = MEMORY_LOAD;
    data_in.request.bits.atomic = '0;
    data_in.request.bits.width = MEMORY_DOUBLE;
    data_in.request.bits.unsigned_0 = 1'b1;
    data_in.request.bits.data = '0;
    data_in.request.bits.destination = DATA_DESTINATION_INTEGER;
    data_in.request.bits.rd = 5'd7;
    data_in.request.bits.floating_point_precision = '0;
    instruction_memory_in = '0;
    instruction_memory_in.request.ready = 1'b1;
    data_memory_in.request.ready = 1'b1;
    data_memory_in.request_fault = 1'b0;
    data_memory_in.request_access_fault = 1'b0;
    data_memory_in.response.valid = pte_response_valid;
    data_memory_in.response.bits.data = pte_response_data;
    data_memory_in.response.bits.destination = '0;
    data_memory_in.response.bits.rd = '0;
    data_memory_in.response.bits.floating_point_precision = '0;
    data_memory_in.drained = !pte_response_valid;
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
      assert (!instruction_memory_out.request.valid)
        else $fatal(1, "data miss unexpectedly issued an instruction-memory request");
      if (data_memory_out.request.valid && data_memory_in.request.ready) begin
        if (page_fault_phase) begin
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
          assert (data_request_valid && data_memory_out.request.bits.address == PHYSICAL_ADDRESS &&
                  data_memory_out.request.bits.destination == DATA_DESTINATION_INTEGER &&
                  data_memory_out.request.bits.rd == 5'd7)
            else $fatal(1, "replayed request was not translated with its metadata intact");
          translated_request_seen <= 1'b1;
        end
      end
    end
  end

  initial begin
    wait (!reset);
    repeat (120) @(posedge clock);
    $fatal(1, "DTLB walk or replay did not complete");
  end

  initial begin
    data_request_valid = 1'b0;
    page_fault_phase = 1'b0;
    privilege = PRIVILEGE_S;
    mstatus = '0;
    satp = SATP_SV39_ROOT_1;
    invalidate_all = 1'b0;
    repeat (2) @(posedge clock);
    #1 reset = 1'b0;

    @(negedge clock);
    data_request_valid = 1'b1;
    #1;
    assert (!data_out.request.ready && !data_out.request_fault &&
            !data_out.request_access_fault && !data_memory_out.request.valid)
      else $fatal(1, "initial DTLB miss was not rejected cleanly");
    @(posedge clock);
    #1 data_request_valid = 1'b0;

    wait (pte_requests == 3 && data_out.drained);
    @(negedge clock);
    data_request_valid = 1'b1;
    #1;
    assert (data_out.request.ready && data_memory_out.request.valid &&
            data_memory_out.request.bits.address == PHYSICAL_ADDRESS)
      else $fatal(1, "replayed request did not hit the filled DTLB");
    @(posedge clock);
    #1 data_request_valid = 1'b0;
    assert (translated_request_seen)
      else $fatal(1, "translated replay was not accepted downstream");

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
    assert (!data_out.drained)
      else $fatal(1, "unrelated permission fault consumed the pending walker fault");

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
    $display("RV5Stage DTLB successful and faulting pulse-and-replay translation passed");
    $finish;
  end
endmodule
