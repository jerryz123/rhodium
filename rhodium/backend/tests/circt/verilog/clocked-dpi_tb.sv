// Simulates clocked DPI updates and verifies disabled calls retain their result state.
// SPDX-License-Identifier: Apache-2.0
module clocked_dpi_tb;
  logic clock = 1'b0;
  logic reset = 1'b0;
  logic enable;
  logic [7:0] value;
  logic [7:0] result;
  logic [7:0] doubled;

  ClockedDPI dut (
    .clock(clock),
    .reset(reset),
    .enable(enable),
    .value(value),
    .result(result),
    .doubled(doubled)
  );

  always #5 clock = ~clock;

  initial begin
    enable = 1'b1;
    value = 8'h10;
    @(posedge clock);
    #1;
    assert (result == 8'h11)
      else $fatal(1, "enabled DPI call did not update its result");
    assert (doubled == 8'h20)
      else $fatal(1, "enabled DPI call did not update its output result");

    enable = 1'b0;
    value = 8'h20;
    @(posedge clock);
    #1;
    assert (result == 8'h11)
      else $fatal(1, "disabled DPI call did not retain its result");
    assert (doubled == 8'h20)
      else $fatal(1, "disabled DPI call did not retain its output result");

    enable = 1'b1;
    @(posedge clock);
    #1;
    assert (result == 8'h21)
      else $fatal(1, "re-enabled DPI call did not update its result");
    assert (doubled == 8'h40)
      else $fatal(1, "re-enabled DPI call did not update its output result");

    $display("clocked DPI simulation passed");
    $finish;
  end
endmodule
