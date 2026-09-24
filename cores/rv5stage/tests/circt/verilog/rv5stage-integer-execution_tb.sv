// Checks elastic integer services and exact five-cycle scalar multiply authorization.
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
  typedef struct packed { mode_t mode; logic high_result, word_result; } scalar_control_t;
  typedef struct packed { logic [63:0] left, right; scalar_control_t control; logic [4:0] rd; } scalar_issue_bits_t;
  typedef struct packed { logic valid; scalar_issue_bits_t bits; } scalar_issue_t;
  typedef struct packed { logic valid; logic [4:0] bits; } scalar_authorize_t;
  typedef struct packed { logic [4:0] rd; logic [63:0] value; } scalar_result_t;
  typedef struct packed { logic valid; scalar_result_t bits; } scalar_completion_t;
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
  scalar_issue_t scalar_issue;
  scalar_authorize_t scalar_authorize;
  scalar_completion_t scalar_completion_out;
  ready_t scalar_completion_in;
  logic scalar_available;
  scalar_issue_t scalar_history[5];
  bit scalar_committed[5];
  int scalar_cycle = 0, scalar_received = 0, scalar_canceled = 0;
  RV5StageIntegerExecutionFixture dut (.scalar_issue_in(scalar_issue), .scalar_authorize_in(scalar_authorize), .*);
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
    scalar_issue = '0;
    scalar_issue.valid = !reset && scalar_cycle < 70 && scalar_cycle % 9 != 8;
    scalar_issue.bits.left = 64'hffffffff00000001 + 64'(scalar_cycle * 113);
    scalar_issue.bits.right = 64'hffffffffffffffd9 + 64'(scalar_cycle);
    scalar_issue.bits.control = 4'(scalar_cycle);
    scalar_issue.bits.control.word_result = scalar_cycle % 8 == 1;
    scalar_issue.bits.rd = 5'(1 + scalar_cycle % 31);
    scalar_authorize.valid = scalar_history[1].valid && scalar_committed[1];
    scalar_authorize.bits = scalar_history[1].bits.rd;
    scalar_completion_in.ready = 1;
  end
  always @(posedge clock) begin
    logic signed [127:0] left, right, dividend, divisor;
    logic [127:0] scalar_product;
    logic [63:0] scalar_value;
    if (reset) begin
      multiply_sent <= 0; divide_sent <= 0; multiply_received <= 0; divide_received <= 0;
      multiply_stalled <= 0; divide_stalled <= 0; replacements <= 0;
      scalar_cycle <= 0; scalar_received <= 0; scalar_canceled <= 0;
      for (int i = 0; i < 5; i++) begin scalar_history[i] <= '0; scalar_committed[i] <= 0; end
    end else begin
      cycles <= cycles + 1;
      scalar_cycle <= scalar_cycle + 1;
      assert(scalar_available) else $fatal(1,"fixed scalar launch acquired a queue stall");
      assert(scalar_completion_out.valid == (scalar_history[4].valid && scalar_committed[4]))
        else $fatal(1,"scalar result not exactly five cycles after its authorized launch at %0d",scalar_cycle);
      if (scalar_completion_out.valid) begin
        left = $signed({64'b0,scalar_history[4].bits.left});
        right = $signed({64'b0,scalar_history[4].bits.right});
        if (scalar_history[4].bits.control.mode.left_signed && left[63]) left -= 128'sd1 << 64;
        if (scalar_history[4].bits.control.mode.right_signed && right[63]) right -= 128'sd1 << 64;
        scalar_product = 128'(left * right);
        scalar_value = scalar_history[4].bits.control.high_result ? scalar_product[127:64] : scalar_product[63:0];
        if (scalar_history[4].bits.control.word_result) scalar_value = {{32{scalar_value[31]}},scalar_value[31:0]};
        assert(scalar_completion_out.bits == scalar_result_t'{scalar_history[4].bits.rd, scalar_value})
          else $fatal(1,"scalar scheduled result or owner differs");
        scalar_received <= scalar_received + 1;
      end
      if (scalar_history[4].valid && !scalar_committed[4]) scalar_canceled <= scalar_canceled + 1;
      for (int i = 4; i > 0; i--) begin scalar_history[i] <= scalar_history[i-1]; scalar_committed[i] <= scalar_committed[i-1]; end
      scalar_history[0] <= scalar_issue;
      scalar_committed[0] <= scalar_cycle % 4 != 0;
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
        assert(scalar_received > 30 && scalar_canceled > 10) else $fatal(1,"scalar authorization/cancellation coverage missing");
        $display("fixed scalar multiply passed: %0d five-cycle returns, %0d canceled launches",scalar_received,scalar_canceled);
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
