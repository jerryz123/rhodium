// Checks opaque tags, simultaneous service progress, held results, replacement, and reset.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_integer_execution_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic left_signed, right_signed; } mode_t;
  typedef struct packed { logic [63:0] left, right; mode_t mode; } multiply_operands_t;
  typedef struct packed { logic [7:0] tag; multiply_operands_t operands; } multiply_request_t;
  typedef struct packed { logic valid; multiply_request_t bits; } multiply_in_t;
  typedef struct packed { logic [63:0] dividend, divisor; logic signed_mode; } divide_operands_t;
  typedef struct packed { logic [7:0] tag; divide_operands_t operands; } divide_request_t;
  typedef struct packed { logic valid; divide_request_t bits; } divide_in_t;
  typedef struct packed { logic [7:0] tag; logic [127:0] value; } multiply_result_t;
  typedef struct packed { logic valid; multiply_result_t bits; } multiply_out_t;
  typedef struct packed { logic [63:0] quotient, remainder_0; } divide_value_t;
  typedef struct packed { logic [7:0] tag; divide_value_t value; } divide_result_t;
  typedef struct packed { logic valid; divide_result_t bits; } divide_out_t;
  logic clock = 0, reset = 1;
  multiply_in_t multiply_request_in;
  divide_in_t divide_request_in;
  ready_t multiply_request_out, divide_request_out, multiply_result_in, divide_result_in;
  multiply_out_t multiply_result_out, held_multiply;
  divide_out_t divide_result_out, held_divide;
  bit multiply_stalled = 0, divide_stalled = 0, drain = 0;
  int multiply_sent = 0, divide_sent = 0, multiply_received = 0, divide_received = 0;
  int cycles = 0, replacements = 0;
  multiply_result_t expected_multiply[24];
  divide_result_t expected_divide[24];
  RV5StageIntegerExecutionFixture dut (.*);
  always #5 clock = ~clock;

  always_comb begin
    multiply_request_in = '0; divide_request_in = '0;
    multiply_request_in.valid = !reset && multiply_sent < 24;
    multiply_request_in.bits.tag = 8'(multiply_sent * 17 + 3);
    multiply_request_in.bits.operands.left = 64'h8000000000000000 + 64'(multiply_sent * 113);
    multiply_request_in.bits.operands.right = 64'hffffffffffffffd9 + 64'(multiply_sent);
    multiply_request_in.bits.operands.mode = 2'(multiply_sent);
    divide_request_in.valid = !reset && divide_sent < 24;
    divide_request_in.bits.tag = 8'(divide_sent * 29 + 7);
    divide_request_in.bits.operands.dividend = 64'h8000000000000000 + 64'(divide_sent * 197);
    divide_request_in.bits.operands.divisor = divide_sent % 3 == 0 ? 0 : divide_sent % 3 == 1 ? -64'd1 : 64'd37;
    divide_request_in.bits.operands.signed_mode = 1'(divide_sent);
    multiply_result_in.ready = drain && cycles % 13 < 4;
    divide_result_in.ready = drain && cycles % 17 < 3;
  end
  always @(posedge clock) begin
    logic signed [127:0] left, right, dividend, divisor;
    if (reset) begin
      multiply_sent <= 0; divide_sent <= 0; multiply_received <= 0; divide_received <= 0;
      multiply_stalled <= 0; divide_stalled <= 0; replacements <= 0;
    end else begin
      cycles <= cycles + 1;
      if (multiply_stalled) assert(multiply_result_out == held_multiply) else $fatal(1,"multiply changed while held");
      if (divide_stalled) assert(divide_result_out == held_divide) else $fatal(1,"divide changed while held");
      held_multiply <= multiply_result_out; held_divide <= divide_result_out;
      multiply_stalled <= multiply_result_out.valid && !multiply_result_in.ready;
      divide_stalled <= divide_result_out.valid && !divide_result_in.ready;
      if (multiply_request_in.valid && multiply_request_out.ready) begin
        left = $signed({64'b0,multiply_request_in.bits.operands.left});
        right = $signed({64'b0,multiply_request_in.bits.operands.right});
        if (multiply_request_in.bits.operands.mode.left_signed && left[63]) left -= 128'sd1 << 64;
        if (multiply_request_in.bits.operands.mode.right_signed && right[63]) right -= 128'sd1 << 64;
        expected_multiply[multiply_sent] = '{multiply_request_in.bits.tag,128'(left*right)};
        multiply_sent <= multiply_sent + 1;
        if (multiply_result_out.valid && multiply_result_in.ready) replacements <= replacements+1;
      end
      if (divide_request_in.valid && divide_request_out.ready) begin
        dividend = $signed({64'b0,divide_request_in.bits.operands.dividend});
        divisor = $signed({64'b0,divide_request_in.bits.operands.divisor});
        if (divide_request_in.bits.operands.signed_mode) begin
          if (dividend[63]) dividend -= 128'sd1 << 64;
          if (divisor[63]) divisor -= 128'sd1 << 64;
        end
        expected_divide[divide_sent] = '{divide_request_in.bits.tag,'{divisor == 0 ? 64'hffffffffffffffff : 64'(dividend/divisor),divisor == 0 ? 64'(dividend) : 64'(dividend%divisor)}};
        divide_sent <= divide_sent + 1;
      end
      if (multiply_result_out.valid && multiply_result_in.ready) begin
        assert(multiply_received < multiply_sent && multiply_result_out.bits == expected_multiply[multiply_received]) else $fatal(1,"multiply ownership/result %0d",multiply_received);
        multiply_received <= multiply_received+1;
      end
      if (divide_result_out.valid && divide_result_in.ready) begin
        assert(divide_received < divide_sent && divide_result_out.bits == expected_divide[divide_received]) else $fatal(1,"divide ownership/result %0d",divide_received);
        divide_received <= divide_received+1;
      end
      if (multiply_received == 24 && divide_received == 24) begin
        assert(replacements != 0) else $fatal(1,"replacement not covered");
        $display("tagged integer execution passed: 48 results, held-result reset, %0d replacements",replacements); $finish;
      end
      if (cycles > 5000) $fatal(1,"tagged execution timeout");
    end
  end
  initial begin
    repeat (4) @(negedge clock); reset = 0;
    wait(multiply_result_out.valid && divide_result_out.valid);
    repeat (5) @(negedge clock); reset = 1;
    repeat (3) @(negedge clock); reset = 0; drain = 1;
  end
endmodule
