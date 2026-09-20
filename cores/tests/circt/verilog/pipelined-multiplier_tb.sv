// Checks feed-forward pipelined signed products, fixed latency, ordering, and reset.
// SPDX-License-Identifier: Apache-2.0
module pipelined_multiplier_tb;
  typedef struct packed { logic left_signed, right_signed; } mode_t;
  typedef struct packed { logic [7:0] left, right; mode_t mode; } request_bits_t;
  typedef struct packed { logic valid; request_bits_t bits; } request_forward_t;
  typedef struct packed { logic valid; logic [15:0] bits; } response_forward_t;

  logic clock = 1'b0, reset = 1'b1;
  request_forward_t request_in;
  response_forward_t response_out;
  logic [15:0] expected[32];
  int sent, received, cycles, response_run;

  PipelinedMultiplier dut (.*);
  always #5 clock = ~clock;

  function automatic logic [15:0] expected_product(input request_bits_t request);
    logic signed [15:0] left, right;
    begin
      left = $signed({8'b0, request.left});
      right = $signed({8'b0, request.right});
      if (request.mode.left_signed && left[7]) left -= 16'sd1 << 8;
      if (request.mode.right_signed && right[7]) right -= 16'sd1 << 8;
      expected_product = 16'(left * right);
    end
  endfunction

  always_comb begin
    request_in = '0;
    request_in.valid = !reset && sent < 32;
    request_in.bits.left = 8'(sent * 37 + 8'h81);
    request_in.bits.right = 8'(sent * 19 + 8'h43);
    request_in.bits.mode = 2'(sent);
  end

  always_ff @(posedge clock) begin
    if (reset) begin
      sent <= 0;
      received <= 0;
      cycles <= 0;
      response_run <= 0;
    end else begin
      cycles <= cycles + 1;
      if (request_in.valid) begin
        expected[sent] <= expected_product(request_in.bits);
        sent <= sent + 1;
      end
      if (cycles < 5)
        assert (!response_out.valid)
          else $fatal(1, "pipelined multiplier responded before five stages");
      if (response_out.valid) begin
        assert (received < sent && response_out.bits == expected[received])
          else $fatal(1, "pipelined multiplier product/order mismatch at %0d", received);
        received <= received + 1;
        response_run <= response_run + 1;
      end
      if (received == 32) begin
        assert (response_run == 32)
          else $fatal(1, "pipelined multiplier inserted a bubble into the response stream");
        $display("pipelined multiplier passed: 32 ordered products");
        $finish;
      end
      if (cycles > 300) $fatal(1, "pipelined multiplier timeout");
    end
  end

  initial begin
    repeat (3) @(negedge clock);
    reset = 1'b0;
  end
endmodule
