// Checks two-client FP service throughput, arithmetic, opaque tags, stalls, fairness, and reset.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_fp_service_tb;
  typedef RV5StageFpExecutionRequest request_t;
  // Describe the public client result, independently of internal tag specializations.
  typedef struct packed {
    logic [7:0] tag;
    logic [63:0] integer_value, fp_value;
    logic [4:0] exception_flags;
    logic exception_flags_valid;
  } result_t;
  typedef struct packed {logic valid; request_t bits;} request_port_t;
  typedef struct packed {logic valid; result_t bits;} result_port_t;
  logic clock = 0, reset = 1;
  request_port_t first_in, second_in;
  struct packed {logic ready;} first_out, second_out;
  struct packed {logic ready;} first_result_in, second_result_in;
  result_port_t first_result_out, second_result_out;
  RV5StageFpServiceFixture dut(.*);
  always #5 clock = ~clock;

  bit pending[2][256];
  request_t accepted[2][256];
  result_port_t held[2];
  bit stalled[2];
  int sent[2], received[2], cycles, consecutive, best_run, last_client;
  int fixed_before_division, blocked_offers, held_cycles, overlap_cycles;
  bit throughput_mode, fixed_stream, division_seen;

  function automatic request_t operation(input int client, input int token, input bit fixed_only);
    request_t req;
    req = '0;
    req.tag = 8'(token);
    req.control.registers.destination = 2'd2;
    req.control.registers.uses_frs1 = 1;
    req.control.registers.uses_frs2 = 1;
    req.control.execution.unit = 4'd2;
    req.control.execution.source_precision = 2'd2;
    req.control.execution.destination_precision = 2'd2;
    req.left = 64'h3ff0000000000000;
    req.right = 64'h4000000000000000;
    // Identical token numbers in both clients deliberately require owner tagging.
    if (!fixed_only) case ((token + client * 5) % 12)
      0: begin // 6 / 2
        req.control.execution.unit = 4'd4;
        req.left = 64'h4018000000000000;
      end
      1: begin end // 1 + 2
      2: begin // 2 * 3
        req.control.execution.unit = 4'd3;
        req.left = 64'h4000000000000000;
        req.right = 64'h4008000000000000;
      end
      3: begin // sqrt(4)
        req.control.execution.unit = 4'd4;
        req.control.execution.divide_operation = 1;
        req.left = 64'h4010000000000000;
      end
      4: begin // 2 * 3 + 1
        req.control.execution.unit = 4'd1;
        req.left = 64'h4000000000000000;
        req.right = 64'h4008000000000000;
        req.third = 64'h3ff0000000000000;
      end
      5: begin // boxed single 1 + 2
        req.control.execution.source_precision = 2'd1;
        req.control.execution.destination_precision = 2'd1;
        req.left = 64'hffffffff3f800000;
        req.right = 64'hffffffff40000000;
      end
      6: begin // divide by zero
        req.control.execution.unit = 4'd4;
        req.right = 0;
      end
      7: begin // sqrt(-1)
        req.control.execution.unit = 4'd4;
        req.control.execution.divide_operation = 1;
        req.left = 64'hbff0000000000000;
      end
      8: begin // raw move to integer, flags remain untouched
        req.control.execution.unit = 4'd9;
        req.control.registers.destination = 2'd1;
        req.left = 64'hbff0000000000000;
      end
      9: begin // incorrectly boxed single operand becomes canonical NaN
        req.control.execution.source_precision = 2'd1;
        req.control.execution.destination_precision = 2'd1;
        req.left = 64'h000000003f800000;
        req.right = 64'hffffffff40000000;
      end
      10: begin // 1 + 2^-53, round upward
        req.right = 64'h3ca0000000000000;
        req.rounding_mode = 3'd3;
      end
      11: begin // 1 < 2, integer result
        req.control.execution.unit = 4'd7;
        req.control.execution.comparison = 2'd1;
        req.control.registers.destination = 2'd1;
      end
    endcase
    return req;
  endfunction

  task automatic check_result(input int client, input result_t value);
    logic [63:0] expected;
    logic [4:0] flags;
    bit flags_valid, integer_result;
    int op;
    assert(pending[client][value.tag]) else $fatal(1, "unsolicited, duplicate, or misrouted result client=%0d tag=%0d", client, value.tag);
    expected = 64'h4008000000000000;
    flags = 0;
    flags_valid = 1;
    integer_result = 0;
    op = (throughput_mode || (fixed_stream && client == 1)) ? 1 : (int'(value.tag) + client * 5) % 12;
    case (op)
      2: expected = 64'h4018000000000000;
      3: expected = 64'h4000000000000000;
      4: expected = 64'h401c000000000000;
      5: expected = 64'hffffffff40400000;
      6: begin expected = 64'h7ff0000000000000; flags = 5'b01000; end
      7: begin expected = 64'h7ff8000000000000; flags = 5'b10000; end
      8: begin expected = 64'hbff0000000000000; flags_valid = 0; integer_result = 1; end
      9: expected = 64'hffffffff7fc00000;
      10: begin expected = 64'h3ff0000000000001; flags = 5'b00001; end
      11: begin expected = 1; integer_result = 1; end
      default: begin end
    endcase
    assert((integer_result ? value.integer_value : value.fp_value) == expected)
      else $fatal(1, "wrong result client=%0d tag=%0d expected=%h got fp=%h integer=%h", client, value.tag, expected, value.fp_value, value.integer_value);
    assert(value.exception_flags == flags && value.exception_flags_valid == flags_valid)
      else $fatal(1, "wrong flags client=%0d tag=%0d", client, value.tag);
    if (!throughput_mode && client == 0 && value.tag == 0) begin
      division_seen = 1;
      if (fixed_stream) assert(received[1] < 120) else $fatal(1, "continuous fixed results starved division");
    end
    if (!throughput_mode && !division_seen && accepted[client][value.tag].control.execution.unit != 4'd4) fixed_before_division++;
    pending[client][value.tag] = 0;
    received[client]++;
  endtask

  task automatic sample;
    request_port_t offers[2];
    result_port_t replies[2];
    bit ready[2], sink_ready[2];
    offers[0] = first_in; offers[1] = second_in;
    ready[0] = first_out.ready; ready[1] = second_out.ready;
    replies[0] = first_result_out; replies[1] = second_result_out;
    sink_ready[0] = first_result_in.ready; sink_ready[1] = second_result_in.ready;
    assert(!(offers[0].valid && ready[0] && offers[1].valid && ready[1]));
    for (int client = 0; client < 2; client++) begin
      if (offers[client].valid && ready[client]) begin
        if (throughput_mode && offers[0].valid && offers[1].valid) begin
          assert(last_client != client) else $fatal(1, "request arbitration failed to rotate");
          last_client = client;
        end
        assert(!pending[client][offers[client].bits.tag]);
        pending[client][offers[client].bits.tag] = 1;
        accepted[client][offers[client].bits.tag] = offers[client].bits;
        sent[client]++;
      end
      if (offers[client].valid && !ready[client]) blocked_offers++;
      if (stalled[client]) begin
        assert(replies[client] === held[client]) else $fatal(1, "stalled result changed for client %0d", client);
        held_cycles++;
      end
      stalled[client] = replies[client].valid && !sink_ready[client];
      held[client] = replies[client];
      if (replies[client].valid && sink_ready[client]) check_result(client, replies[client].bits);
    end
    if ((first_in.valid && first_out.ready) || (second_in.valid && second_out.ready)) begin
      consecutive++;
      if (consecutive > best_run) best_run = consecutive;
      if ((first_result_out.valid && first_result_in.ready) || (second_result_out.valid && second_result_in.ready)) overlap_cycles++;
    end else consecutive = 0;
    cycles++;
  endtask

  task automatic clear_epoch;
    first_in = '0; second_in = '0;
    first_result_in = '0; second_result_in = '0;
    reset = 1;
    repeat (3) @(negedge clock);
    reset = 0;
    foreach (pending[c, t]) pending[c][t] = 0;
    for (int c = 0; c < 2; c++) begin sent[c] = 0; received[c] = 0; stalled[c] = 0; end
    cycles = 0; consecutive = 0; best_run = 0; last_client = -1;
    division_seen = 0; fixed_before_division = 0;
    fixed_stream = 0;
    blocked_offers = 0; held_cycles = 0; overlap_cycles = 0;
  endtask

  task automatic run_batch(input int count, input bit fixed_only);
    throughput_mode = fixed_only;
    while (received[0] < count || received[1] < count) begin
      first_in.valid = sent[0] < count;
      first_in.bits = operation(0, sent[0], fixed_only);
      second_in.valid = sent[1] < count;
      second_in.bits = operation(1, sent[1], fixed_only);
      first_result_in.ready = fixed_only || (cycles > 30 && cycles % 17 < 11);
      second_result_in.ready = fixed_only || (cycles > 45 && cycles % 19 < 10);
      @(posedge clock);
      sample();
      @(negedge clock);
      assert(cycles < 10000) else $fatal(1, "service failed to drain");
    end
    first_in.valid = 0; second_in.valid = 0;
    first_result_in.ready = 1; second_result_in.ready = 1;
    repeat (12) begin @(posedge clock); sample(); @(negedge clock); end
  endtask

  initial begin
    clear_epoch();
    run_batch(48, 1);
    assert(best_run >= 32 && overlap_cycles >= 32) else $fatal(1, "fixed execution lost one-per-cycle throughput: %0d", best_run);
    clear_epoch();
    throughput_mode = 0;
    fixed_stream = 1;
    while (received[0] < 1 || received[1] < 128) begin
      first_in.valid = sent[0] == 0;
      first_in.bits = operation(0, 0, 0);
      second_in.valid = sent[1] < 128;
      second_in.bits = operation(1, sent[1], 1);
      first_result_in.ready = 1; second_result_in.ready = 1;
      @(posedge clock); sample(); @(negedge clock);
      assert(cycles < 1000) else $fatal(1, "fair completion failed to drain");
    end
    assert(division_seen && fixed_before_division > 0);
    clear_epoch();
    run_batch(96, 0);
    assert(division_seen && fixed_before_division > 0 && held_cycles > 20 && blocked_offers > 20 && overlap_cycles > 0)
      else $fatal(1, "missing overlap/backpressure/reordering coverage");

    // Reset while one variable-latency request and buffered fixed work are owned.
    clear_epoch();
    throughput_mode = 0;
    for (int n = 0; n < 12; n++) begin
      first_in.valid = sent[0] == 0;
      first_in.bits = operation(0, 0, 0);
      second_in.valid = sent[1] < 4;
      second_in.bits = operation(1, sent[1], 1);
      @(posedge clock); sample(); @(negedge clock);
    end
    assert(sent[0] == 1 && sent[1] > 0 && received[0] == 0 && received[1] == 0);
    clear_epoch();
    first_result_in.ready = 1; second_result_in.ready = 1;
    repeat (100) begin
      @(posedge clock);
      assert(!first_result_out.valid && !second_result_out.valid) else $fatal(1, "pre-reset completion leaked");
      @(negedge clock);
    end
    run_batch(12, 0);
    $display("shared FP service passed: 441 tagged results, concurrent clients, fixed throughput, fair completion, stalls, and reset");
    $finish;
  end
  initial begin #1000000; $fatal(1, "timeout"); end
endmodule
