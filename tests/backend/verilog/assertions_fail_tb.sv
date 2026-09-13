// Activates a branch assertion with a false condition and expects its labeled failure.
// SPDX-License-Identifier: Apache-2.0
module assertions_fail_tb;
  logic clock = 1'b0;
  logic reset = 1'b1;
  logic always_condition = 1'b1;
  logic request = 1'b1;
  logic request_condition = 1'b0;

  Assertions dut (
    .clock(clock),
    .reset(reset),
    .always_condition(always_condition),
    .request(request),
    .request_condition(request_condition)
  );

  always #5 clock = ~clock;

  initial begin
    repeat (2) @(posedge clock);
    #1 reset = 1'b0;
    @(posedge clock);
    #1 $finish;
  end
endmodule
