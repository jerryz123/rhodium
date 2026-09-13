// Exercises unsigned, signed, exceptional, and backpressured divider transactions.
// SPDX-License-Identifier: Apache-2.0
module iterative_divider_tb;
  typedef struct packed {
    logic [7:0] dividend;
    logic [7:0] divisor;
    logic signed_mode;
  } request_bits_t;
  typedef struct packed {
    logic valid;
    request_bits_t bits;
  } request_forward_t;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed {
    logic [7:0] quotient;
    logic [7:0] remainder;
  } response_bits_t;
  typedef struct packed {
    logic valid;
    response_bits_t bits;
  } response_forward_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  request_forward_t request_in;
  ready_t request_out;
  ready_t response_in;
  response_forward_t response_out;

  IterativeDivider dut (.*);
  always #5 clock = ~clock;

  task automatic tick;
    @(posedge clock);
    #1;
  endtask

  task automatic issue(
    input logic [7:0] dividend,
    input logic [7:0] divisor,
    input logic signed_mode
  );
    while (!request_out.ready)
      tick();
    request_in = '{valid: 1'b1, bits: '{dividend, divisor, signed_mode}};
    tick();
    request_in.valid = 1'b0;
  endtask

  task automatic expect_result_after(
    input integer wait_cycles,
    input logic [7:0] quotient,
    input logic [7:0] remainder
  );
    repeat (wait_cycles) begin
      assert (!response_out.valid)
        else $fatal(1, "divider response arrived before its expected latency");
      assert (!request_out.ready)
        else $fatal(1, "divider accepted a request while active");
      tick();
    end
    assert (response_out.valid && response_out.bits.quotient == quotient &&
            response_out.bits.remainder == remainder)
      else $fatal(1, "divider result mismatch");
  endtask

  function automatic integer significant_bits(input logic [7:0] value);
    significant_bits = 0;
    for (integer index = 0; index < 8; index++) begin
      if (value[index])
        significant_bits = index + 1;
    end
  endfunction

  task automatic expect_reference(
    input logic [7:0] dividend,
    input logic [7:0] divisor,
    input logic signed_mode
  );
    logic signed [7:0] signed_dividend;
    logic signed [7:0] signed_divisor;
    logic [7:0] dividend_magnitude;
    logic [7:0] divisor_magnitude;
    logic [7:0] expected_quotient;
    logic [7:0] expected_remainder;
    integer expected_wait;
    integer observed_wait;

    signed_dividend = dividend;
    signed_divisor = divisor;
    dividend_magnitude = signed_mode && dividend[7] ? -dividend : dividend;
    divisor_magnitude = signed_mode && divisor[7] ? -divisor : divisor;
    if (divisor == 0) begin
      expected_quotient = '1;
      expected_remainder = dividend;
    end else if (signed_mode && dividend == 8'h80 && divisor == 8'hff) begin
      expected_quotient = 8'h80;
      expected_remainder = 8'h00;
    end else if (signed_mode) begin
      expected_quotient = signed_dividend / signed_divisor;
      expected_remainder = signed_dividend % signed_divisor;
    end else begin
      expected_quotient = dividend / divisor;
      expected_remainder = dividend % divisor;
    end
    expected_wait = divisor_magnitude == 0 || divisor_magnitude == 1 ||
                    dividend_magnitude <= divisor_magnitude
                  ? 0 : significant_bits(dividend_magnitude) + 1;
    observed_wait = 0;
    while (!response_out.valid) begin
      assert (!request_out.ready && observed_wait < 9)
        else $fatal(1, "divider exceeded its normalized iteration latency");
      tick();
      observed_wait++;
    end
    assert (observed_wait == expected_wait)
      else $fatal(1, "divider latency mismatch: got %0d expected %0d",
                  observed_wait, expected_wait);
    assert (response_out.bits.quotient == expected_quotient &&
            response_out.bits.remainder == expected_remainder)
      else $fatal(1, "divider reference mismatch for %h / %h signed=%b",
                  dividend, divisor, signed_mode);
  endtask

  initial begin
    request_in = '0;
    response_in = '{ready: 1'b1};
    tick();
    reset = 1'b0;

    issue(8'd100, 8'd7, 1'b0);
    expect_result_after(8, 8'd14, 8'd2);

    issue(-8'sd100, 8'd7, 1'b1);
    expect_result_after(8, -8'sd14, -8'sd2);

    issue(8'd100, -8'sd7, 1'b1);
    expect_result_after(8, -8'sd14, 8'd2);

    issue(8'hff, 8'd16, 1'b0);
    expect_result_after(9, 8'd15, 8'd15);

    issue(8'hfd, 8'd0, 1'b1);
    expect_result_after(0, 8'hff, 8'hfd);

    issue(8'd3, 8'd7, 1'b0);
    expect_result_after(0, 8'd0, 8'd3);

    issue(-8'sd7, 8'd7, 1'b1);
    expect_result_after(0, -8'sd1, 8'd0);

    issue(8'h80, 8'hff, 1'b1);
    response_in.ready = 1'b0;
    #1;
    expect_result_after(0, 8'h80, 8'h00);
    repeat (3) begin
      tick();
      assert (response_out.valid && response_out.bits.quotient == 8'h80 &&
              response_out.bits.remainder == 8'h00)
        else $fatal(1, "backpressured divider response was not stable");
      assert (!request_out.ready)
        else $fatal(1, "held divider response did not block another request");
    end

    response_in.ready = 1'b1;
    #1;
    assert (request_out.ready)
      else $fatal(1, "divider could not replace a consumed response");
    request_in = '{valid: 1'b1, bits: '{8'd37, 8'd5, 1'b0}};
    tick();
    request_in.valid = 1'b0;
    assert (!response_out.valid)
      else $fatal(1, "consumed divider response remained valid");
    expect_result_after(7, 8'd7, 8'd2);

    for (integer signed_mode = 0; signed_mode < 2; signed_mode++) begin
      for (integer dividend = 0; dividend < 256; dividend++) begin
        for (integer divisor = 0; divisor < 256; divisor++) begin
          issue(dividend[7:0], divisor[7:0], signed_mode[0]);
          expect_reference(dividend[7:0], divisor[7:0], signed_mode[0]);
        end
      end
    end

    tick();
    assert (!response_out.valid && request_out.ready)
      else $fatal(1, "divider did not return to idle");

    $display("iterative divider passed");
    $finish;
  end
endmodule
