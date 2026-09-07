// Checks captured operands, preparation latency, reset, signed products, and response backpressure.
module iterative_multiplier_tb;
  typedef struct packed {
    logic left_signed;
    logic right_signed;
  } mode_t;
  typedef struct packed {
    logic [7:0] left;
    logic [7:0] right;
    mode_t mode;
  } request_bits_t;
  typedef struct packed {
    logic valid;
    request_bits_t bits;
  } request_forward_t;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed {
    logic valid;
    logic [15:0] bits;
  } response_forward_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  request_forward_t request_in;
  ready_t request_out;
  ready_t response_in;
  response_forward_t response_out;

  IterativeMultiplier dut (.*);
  always #5 clock = ~clock;

  task automatic tick;
    @(posedge clock);
    #1;
  endtask

  task automatic issue(
    input logic [7:0] left,
    input logic [7:0] right,
    input logic left_signed,
    input logic right_signed
  );
    while (!request_out.ready)
      tick();
    request_in = '{valid: 1'b1, bits: '{left, right, '{left_signed, right_signed}}};
    tick();
    request_in.valid = 1'b0;
    request_in.bits = '0; // Preparation must use the accepted operands, not the live request.
  endtask

  task automatic expect_product(input logic [15:0] expected);
    repeat (9) begin
      assert (!response_out.valid)
        else $fatal(1, "multiplier response arrived before preparation and eight iterations");
      assert (!request_out.ready)
        else $fatal(1, "multiplier accepted a request while active");
      tick();
    end
    assert (response_out.valid && response_out.bits == expected)
      else $fatal(1, "multiplier product mismatch");
  endtask

  initial begin
    request_in = '0;
    response_in = '{ready: 1'b1};
    tick();
    reset = 1'b0;

    issue(8'd7, 8'd9, 1'b0, 1'b0);
    expect_product(16'h003f);

    issue(8'hff, 8'hff, 1'b0, 1'b0);
    expect_product(16'hfe01);

    issue(8'hfd, 8'd7, 1'b1, 1'b0);
    expect_product(16'hffeb);

    issue(8'h80, 8'hff, 1'b1, 1'b1);
    response_in.ready = 1'b0;
    #1;
    expect_product(16'h0080);
    repeat (3) begin
      tick();
      assert (response_out.valid && response_out.bits == 16'h0080)
        else $fatal(1, "backpressured response was not stable");
      assert (!request_out.ready)
        else $fatal(1, "held response did not block another request");
    end

    response_in.ready = 1'b1;
    #1;
    assert (request_out.ready)
      else $fatal(1, "multiplier could not replace a consumed response");
    request_in = '{valid: 1'b1, bits: '{8'hff, 8'hfe, '{1'b0, 1'b1}}};
    tick();
    request_in.valid = 1'b0;
    assert (!response_out.valid)
      else $fatal(1, "consumed response remained valid");
    expect_product(16'hfe02);

    issue(8'h80, 8'h80, 1'b1, 1'b1);
    expect_product(16'h4000);
    issue(8'h00, 8'h80, 1'b0, 1'b1);
    expect_product(16'h0000);

    // Reset between acceptance and magnitude preparation must cancel the request.
    issue(8'h81, 8'h7f, 1'b1, 1'b1);
    reset = 1'b1;
    tick();
    reset = 1'b0;
    repeat (10) begin
      assert (!response_out.valid && request_out.ready)
        else $fatal(1, "reset did not cancel multiplier preparation");
      tick();
    end
    issue(8'hff, 8'hff, 1'b1, 1'b1);
    expect_product(16'h0001);

    tick();
    assert (!response_out.valid && request_out.ready)
      else $fatal(1, "multiplier did not return to idle");

    $display("iterative multiplier passed");
    $finish;
  end
endmodule
