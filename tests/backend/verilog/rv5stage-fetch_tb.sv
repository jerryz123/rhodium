// Checks fixed-latency fetch assembly, completed-word capacity, restart, and faults.
module rv5stage_fetch_tb;
  typedef struct packed { logic [63:0] address; } request_bits_t;
  typedef struct packed { logic valid; request_bits_t bits; } request_t;
  typedef struct packed { logic [31:0] word; logic page_fault; logic access_fault; } response_bits_t;
  typedef struct packed { logic valid; response_bits_t bits; } response_t;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { response_bits_t response; logic replay; } result_bits_t;
  typedef struct packed { logic valid; result_bits_t bits; } result_t;
  typedef struct packed { ready_t request; result_t response; } memory_in_t;
  typedef struct packed { logic flush; logic invalidate_all; logic s1_kill; request_t request; } memory_out_t;
  typedef struct packed {
    logic [63:0] pc;
    logic [31:0] instruction;
    logic [31:0] raw_instruction;
    logic [63:0] sequential_pc;
    logic [63:0] predicted_next_pc;
    logic compressed_illegal;
    logic instruction_page_fault;
    logic instruction_access_fault;
    logic [63:0] instruction_fault_address;
  } fetched_bits_t;
  typedef struct packed { logic valid; fetched_bits_t bits; } fetched_out_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  logic active = 1'b1;
  logic flush = 1'b0;
  logic restart_valid = 1'b0;
  logic [63:0] restart_pc = '0;
  logic invalidate_all = 1'b0;
  logic predictor_flush = 1'b0;
  logic [132:0] branch_update_in = '0;
  memory_in_t memory_in;
  ready_t fetched_in;
  memory_out_t memory_out;
  fetched_out_t fetched_out;
  logic response_valid;
  response_t s2_response;
  response_bits_t response_bits;
  logic fetched_ready = 1'b1;
  integer stalled_requests;
  logic [63:0] held_request_address;

  RV5StageFrontend dut (.control_in({active, flush, restart_valid, restart_pc, invalidate_all, predictor_flush, branch_update_in}), .*);
  always #5 clock = ~clock;

  function automatic logic [31:0] word_at(input logic [63:0] address);
    case (address)
      64'h0000: word_at = 32'h00850085;
      64'h0004: word_at = 32'h03130001;
      64'h0008: word_at = 32'h90020010;
      64'h0100: word_at = 32'h00000013;
      64'h0104: word_at = 32'h00100093;
      64'h0108: word_at = 32'h00200113;
      64'h0ffc: word_at = 32'h03130001;
      default: word_at = 32'h00000013;
    endcase
  endfunction

  always_comb begin
    memory_in.request.ready = 1'b1;
    memory_in.response.valid = s2_response.valid;
    memory_in.response.bits = '{s2_response.bits, 1'b0};
    fetched_in.ready = fetched_ready;
  end

  always_ff @(posedge clock) begin
    if (reset || memory_out.flush) begin
      response_valid <= 1'b0;
      s2_response <= '0;
      response_bits <= '0;
    end else begin
      s2_response <= memory_out.s1_kill ? '0 : '{response_valid, response_bits};
      response_valid <= 1'b0;
      if (memory_out.request.valid && memory_in.request.ready) begin
        response_valid <= 1'b1;
        response_bits.word <= word_at(memory_out.request.bits.address);
        response_bits.page_fault <= memory_out.request.bits.address == 64'h1000;
        response_bits.access_fault <= 1'b0;
        assert (memory_out.request.bits.address[1:0] == 2'b00)
          else $fatal(1, "fetch request was not word aligned");
      end
    end
  end

  task automatic expect_instruction(input logic [63:0] pc,
                                    input logic [63:0] sequential_pc,
                                    input logic [31:0] raw_instruction,
                                    input logic [31:0] instruction);
    wait (fetched_out.valid);
    #1;
    assert (fetched_out.bits.pc == pc &&
            fetched_out.bits.sequential_pc == sequential_pc &&
            fetched_out.bits.raw_instruction == raw_instruction &&
            fetched_out.bits.instruction == instruction &&
            !fetched_out.bits.compressed_illegal &&
            !fetched_out.bits.instruction_page_fault &&
            !fetched_out.bits.instruction_access_fault)
      else $fatal(1, "fetch mismatch pc=%h next=%h raw=%h instruction=%h",
                  fetched_out.bits.pc,
                  fetched_out.bits.sequential_pc,
                  fetched_out.bits.raw_instruction,
                  fetched_out.bits.instruction);
    @(posedge clock);
    #1;
  endtask

  initial begin
    repeat (2) @(posedge clock);
    reset = 1'b0;
    restart_pc = 64'h0;
    restart_valid = 1'b1;
    @(posedge clock);
    #1 restart_valid = 1'b0;

    expect_instruction(64'h0, 64'h2, 32'h00000085, 32'h00108093);
    expect_instruction(64'h2, 64'h4, 32'h00000085, 32'h00108093);
    expect_instruction(64'h4, 64'h6, 32'h00000001, 32'h00000013);
    expect_instruction(64'h6, 64'ha, 32'h00100313, 32'h00100313);
    expect_instruction(64'ha, 64'hc, 32'h00009002, 32'h00100073);

    restart_pc = 64'h100;
    restart_valid = 1'b1;
    @(posedge clock);
    #1 restart_valid = 1'b0;
    wait (fetched_out.valid && fetched_out.bits.pc == 64'h100);
    #1;
    assert (fetched_out.bits.instruction == 32'h00000013);
    held_request_address = memory_out.request.bits.address;
    active = 1'b0;
    #1;
    assert (memory_out.request.bits.address == held_request_address)
      else $fatal(1, "same-cycle squash selected a different fetch address");
    active = 1'b1;
    @(posedge clock);
    #1;
    assert (fetched_out.valid && fetched_out.bits.pc == 64'h104 &&
            fetched_out.bits.instruction == 32'h00100093)
      else $fatal(1, "fetch window inserted a bubble after pc 0x100");
    @(posedge clock);
    #1;
    assert (fetched_out.valid && fetched_out.bits.pc == 64'h108 &&
            fetched_out.bits.instruction == 32'h00200113)
      else $fatal(1, "fetch window inserted a bubble after pc 0x104");

    fetched_ready = 1'b0;
    stalled_requests = 0;
    restart_pc = 64'h200;
    restart_valid = 1'b1;
    @(posedge clock);
    #1 restart_valid = 1'b0;
    repeat (24) begin
      @(posedge clock);
      if (memory_out.request.valid && memory_in.request.ready)
        stalled_requests = stalled_requests + 1;
      #1;
      if (fetched_out.valid)
        assert (fetched_out.bits.pc == 64'h200 &&
                fetched_out.bits.instruction == 32'h00000013)
          else $fatal(1, "fetch queue head changed under backpressure pc=%h",
                      fetched_out.bits.pc);
    end
    assert (stalled_requests == 5)
      else $fatal(1, "fetch did not run ahead while Decode was stalled requests=%0d",
                  stalled_requests);
    assert (fetched_out.valid && fetched_out.bits.pc == 64'h200)
      else $fatal(1, "fetch queue did not retain its stalled head");
    assert (!memory_out.request.valid)
      else $fatal(1, "fetch did not stop after exhausting bounded queue capacity");

    fetched_ready = 1'b1;
    expect_instruction(64'h200, 64'h204, 32'h00000013, 32'h00000013);
    expect_instruction(64'h204, 64'h208, 32'h00000013, 32'h00000013);
    expect_instruction(64'h208, 64'h20c, 32'h00000013, 32'h00000013);
    expect_instruction(64'h20c, 64'h210, 32'h00000013, 32'h00000013);
    expect_instruction(64'h210, 64'h214, 32'h00000013, 32'h00000013);

    restart_pc = 64'hffe;
    restart_valid = 1'b1;
    @(posedge clock);
    #1 restart_valid = 1'b0;
    wait (fetched_out.valid);
    #1;
    assert (fetched_out.bits.pc == 64'hffe &&
            fetched_out.bits.instruction_page_fault &&
            fetched_out.bits.instruction_fault_address == 64'h1000)
      else $fatal(1, "cross-page fault mismatch pc=%h fault=%b address=%h",
                  fetched_out.bits.pc,
                  fetched_out.bits.instruction_page_fault,
                  fetched_out.bits.instruction_fault_address);
    $finish;
  end
endmodule
