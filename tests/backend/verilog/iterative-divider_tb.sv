// Exercises unsigned, signed, exceptional, and backpressured divider transactions.
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

  task automatic expect_result(
    input logic [7:0] quotient,
    input logic [7:0] remainder
  );
    repeat (8) begin
      assert (!response_out.valid)
        else $fatal(1, "divider response arrived before eight iterations");
      assert (!request_out.ready)
        else $fatal(1, "divider accepted a request while active");
      tick();
    end
    assert (!response_out.valid && !request_out.ready)
      else $fatal(1, "divider skipped its registered finalization cycle");
    tick();
    assert (response_out.valid && response_out.bits.quotient == quotient &&
            response_out.bits.remainder == remainder)
      else $fatal(1, "divider result mismatch");
  endtask

  initial begin
    request_in = '0;
    response_in = '{ready: 1'b1};
    tick();
    reset = 1'b0;

    issue(8'd100, 8'd7, 1'b0);
    expect_result(8'd14, 8'd2);

    issue(-8'sd100, 8'd7, 1'b1);
    expect_result(-8'sd14, -8'sd2);

    issue(8'd100, -8'sd7, 1'b1);
    expect_result(-8'sd14, 8'd2);

    issue(8'hff, 8'd16, 1'b0);
    expect_result(8'd15, 8'd15);

    issue(8'hfd, 8'd0, 1'b1);
    expect_result(8'hff, 8'hfd);

    issue(8'h80, 8'hff, 1'b1);
    response_in.ready = 1'b0;
    #1;
    expect_result(8'h80, 8'h00);
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
    expect_result(8'd7, 8'd2);

    tick();
    assert (!response_out.valid && request_out.ready)
      else $fatal(1, "divider did not return to idle");

    $display("iterative divider passed");
    $finish;
  end
endmodule
