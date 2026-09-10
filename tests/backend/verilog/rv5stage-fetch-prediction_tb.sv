// Checks predicted streams, completed-word compaction, compressed cuts, stalls, and repair.
module rv5stage_fetch_prediction_tb;
  typedef struct packed { logic [63:0] address; } request_bits_t;
  typedef struct packed { logic valid; request_bits_t bits; } request_t;
  typedef struct packed { logic [31:0] word; logic page_fault, access_fault; } response_bits_t;
  typedef struct packed { logic valid; response_bits_t bits; } response_t;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { response_bits_t response; logic replay; } result_bits_t;
  typedef struct packed { logic valid; result_bits_t bits; } result_t;
  typedef struct packed { ready_t request; result_t response; } memory_in_t;
  typedef struct packed { logic flush, invalidate_all, s1_kill; request_t request; } memory_out_t;
  typedef struct packed {
    logic [63:0] pc;
    logic [31:0] instruction, raw_instruction;
    logic [63:0] sequential_pc, predicted_next_pc;
    logic compressed_illegal, instruction_page_fault, instruction_access_fault;
    logic [63:0] instruction_fault_address;
  } fetched_bits_t;
  typedef struct packed { logic valid; fetched_bits_t bits; } fetched_out_t;
  typedef struct packed { logic [63:0] pc, target; logic branch, conditional, taken, compressed; } update_bits_t;
  typedef struct packed { logic valid; update_bits_t bits; } update_t;
  logic clock = 0, reset = 1, active = 0, flush = 0, restart_valid = 0;
  logic invalidate_all = 0, predictor_flush = 0;
  logic [63:0] restart_pc = 0;
  update_t branch_update_in = '0;
  memory_in_t memory_in;
  memory_out_t memory_out;
  ready_t fetched_in;
  fetched_out_t fetched_out;
  response_t response = '0, s2_response = '0;
  bit request_ready = 1, output_ready = 1, fault_continuation = 0;
  int mode = 0, cycle = 0, previous_request = -1, previous_output = -1;
  int request_checks = 0, output_checks = 0, local_flushes = 0;
  bit continuous_requests = 0, continuous_outputs = 0;
  logic [63:0] expected_requests[$];
  RV5StageFrontend dut (.control_in({active, flush, restart_valid, restart_pc, invalidate_all, predictor_flush, branch_update_in}), .*);
  always #5 clock = ~clock;

  function automatic logic [31:0] word_at(input logic [63:0] address);
    case (mode)
      0: return 32'h0000006f; // JAL at each word boundary.
      1: case (address)
        'h100: return 32'ha001c001; // Conditional compressed branch, then C.J.
        'h200: return 32'ha0010001; // Enter at the upper C.J.
        default: return 32'h00000013;
      endcase
      2: case (address)
        'h300: return 32'h006f0001; // 32-bit JAL starts at +2.
        'h304: return 32'h00010000; // Continuation, upper half must be discarded.
        'h400: return 32'ha0010001;
        default: return 32'h00000013;
      endcase
      default: return 32'h00000013;
    endcase
  endfunction
  always_comb begin
    memory_in.request.ready = request_ready;
    memory_in.response = '{s2_response.valid, '{s2_response.bits, 1'b0}};
    fetched_in.ready = output_ready;
  end
  always @(posedge clock) begin
    cycle = cycle + 1;
    if (reset || memory_out.flush) begin response <= '0; s2_response <= '0; end
    else begin
      s2_response <= memory_out.s1_kill ? '0 : response;
      response.valid <= 0;
      if (memory_out.request.valid && memory_in.request.ready)
        response <= '{1'b1, '{word_at(memory_out.request.bits.address), fault_continuation && memory_out.request.bits.address == 'h304, 1'b0}};
    end
    if (!reset && memory_out.flush && !restart_valid && !flush) local_flushes = local_flushes + 1;
    if (!reset && memory_out.request.valid && memory_in.request.ready && expected_requests.size() != 0) begin
      automatic logic [63:0] expected = expected_requests.pop_front();
      assert (memory_out.request.bits.address == expected)
        else $fatal(1, "request got %h expected %h", memory_out.request.bits.address, expected);
      if (continuous_requests && previous_request >= 0)
        assert (cycle == previous_request + 1) else $fatal(1, "prediction inserted a request bubble");
      previous_request = cycle;
      request_checks = request_checks + 1;
    end
    if (!reset && fetched_out.valid && fetched_in.ready && continuous_outputs) begin
      if (previous_output >= 0)
        assert (cycle == previous_output + 1) else $fatal(1, "prediction inserted an output bubble");
      previous_output = cycle;
      output_checks = output_checks + 1;
    end
  end
  task automatic initialize(input int next_mode);
    @(negedge clock);
    reset = 1; active = 0; mode = next_mode; output_ready = 1; request_ready = 1;
    continuous_requests = 0; continuous_outputs = 0; fault_continuation = 0;
    expected_requests.delete(); previous_request = -1; previous_output = -1;
    request_checks = 0; output_checks = 0; local_flushes = 0;
    repeat (2) @(negedge clock);
    reset = 0;
  endtask
  task automatic train(input logic [63:0] pc, target, input bit compressed, conditional = 0, taken = 1);
    branch_update_in = '{1'b1, '{pc, target, 1'b1, conditional, taken, compressed}};
    @(negedge clock);
    branch_update_in = '0;
  endtask
  task automatic start(input logic [63:0] pc);
    restart_valid = 1; restart_pc = pc;
    @(negedge clock);
    restart_valid = 0; active = 1;
  endtask
  task automatic expect_pc(input logic [63:0] pc, npc, input bit fault = 0);
    @(negedge clock);
    while (!fetched_out.valid) @(negedge clock);
    assert (fetched_out.bits.pc == pc && fetched_out.bits.predicted_next_pc == npc && fetched_out.bits.instruction_page_fault == fault)
      else $fatal(1, "output pc=%h npc=%h fault=%b, expected pc=%h npc=%h fault=%b", fetched_out.bits.pc, fetched_out.bits.predicted_next_pc, fetched_out.bits.instruction_page_fault, pc, npc, fault);
    if (fault)
      assert (fetched_out.bits.instruction_fault_address == 'h304) else $fatal(1, "wrong continuation fault address");
  endtask
  initial begin
    initialize(0);
    train('h100, 'h200, 0);
    train('h200, 'h100, 0);
    repeat (8) begin expected_requests.push_back('h100); expected_requests.push_back('h200); end
    continuous_requests = 1; continuous_outputs = 1;
    start('h100);
    repeat (8) begin expect_pc('h100, 'h200); expect_pc('h200, 'h100); end
    assert (request_checks == 16 && output_checks >= 15 && local_flushes == 0);
    continuous_outputs = 0; continuous_requests = 0;

    // Ready stalls hold the request address even while the table is trained.
    request_ready = 0; output_ready = 0;
    begin
      automatic logic [63:0] held = memory_out.request.bits.address;
      train('h100, 'h800, 0);
      repeat (4) begin @(negedge clock); assert (memory_out.request.bits.address == held); end
    end
    // Already accepted occurrences still carry their original target.
    output_ready = 1;
    expect_pc('h100, 'h200);

    initialize(1);
    train('h100, 'h900, 1, 1);
    train('h100, 'h900, 1, 1, 0);
    train('h102, 'h202, 1);
    train('h202, 'h100, 1);
    expected_requests = '{'h100, 'h200, 'h100, 'h200};
    start('h100);
    repeat (2) begin expect_pc('h100, 'h102); expect_pc('h102, 'h202); expect_pc('h202, 'h100); end
    assert (request_checks == 4 && local_flushes == 0);

    initialize(2);
    train('h302, 'h402, 0);
    train('h402, 'h302, 1);
    // Repeated three-word loops exercise two-word compaction while subsequent
    // occurrences retain their own prediction and continuation metadata.
    repeat (10) begin expected_requests.push_back('h300); expected_requests.push_back('h304); expected_requests.push_back('h400); end
    start('h302);
    repeat (10) begin expect_pc('h302, 'h402); expect_pc('h402, 'h302); end
    assert (request_checks == 30 && local_flushes == 0);

    initialize(3);
    train('h502, 'h600, 1); // Stale prediction in the middle of ADDI.
    output_ready = 0;
    start('h4fc);
    repeat (15) @(negedge clock);
    // Assembly stops at the held older word. Repair the younger cut when it
    // reaches the head, without discarding or changing that older instruction.
    assert (fetched_out.valid && fetched_out.bits.pc == 'h4fc) else $fatal(1, "repair discarded older queued instruction");
    output_ready = 1;
    expect_pc('h500, 'h504);
    assert (local_flushes == 1) else $fatal(1, "stale cut did not repair exactly once");
    expect_pc('h504, 'h508);

    initialize(2);
    train('h302, 'h402, 1); // Stale short length must not truncate the JAL.
    start('h302);
    expect_pc('h302, 'h306);
    assert (local_flushes == 1);

    initialize(2);
    train('h304, 'h800, 1); // Cut in the continuation halfword.
    start('h302);
    expect_pc('h302, 'h306);
    assert (local_flushes == 1);

    initialize(2);
    train('h302, 'h402, 0);
    fault_continuation = 1;
    start('h302);
    expect_pc('h302, 'h306, 1);

    initialize(3);
    train('h502, 'h600, 1);
    start('h500);
    while (!memory_out.flush) @(negedge clock);
    restart_valid = 1; restart_pc = 'h700; // Architectural recovery overrides local repair.
    @(negedge clock);
    restart_valid = 0;
    expect_pc('h700, 'h704);
    $display("RV5Stage bubbleless predicted fetch, compressed streams, stalls and repair passed");
    $finish;
  end
  initial begin #50000; $fatal(1, "predicted fetch timeout"); end
endmodule
