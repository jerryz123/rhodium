// Simulates reset suppression and both vacuous and active implication checks.
// SPDX-License-Identifier: Apache-2.0
module assertions_tb;
  logic clock = 1'b0;
  logic reset = 1'b1;
  logic always_condition = 1'b0;
  logic request = 1'b0;
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
    #1;

    always_condition = 1'b1;
    reset = 1'b0;
    repeat (2) @(posedge clock);
    #1;

    request_condition = 1'b1;
    request = 1'b1;
    repeat (2) @(posedge clock);
    #1;

    $display("assertion simulation passed");
    $finish;
  end
endmodule
