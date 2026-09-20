// Simulates enable, non-power-of-two rollover, wrap indication, and reset.
// SPDX-License-Identifier: Apache-2.0
module standard_counter_tb;
  logic       clock = 1'b0;
  logic       reset = 1'b1;
  logic       enable = 1'b0;
  logic [3:0] value;
  logic       wrap;

  Counter dut (
    .clock  (clock),
    .reset  (reset),
    .enable (enable),
    .value  (value),
    .wrap   (wrap)
  );

  always #5 clock = ~clock;

  task automatic tick;
    @(posedge clock);
    #1;
  endtask

  initial begin
    tick();
    reset = 1'b0;
    assert (value == 4'd0 && !wrap)
      else $fatal(1, "counter did not reset to zero");

    repeat (2) tick();
    assert (value == 4'd0)
      else $fatal(1, "disabled counter did not hold");

    enable = 1'b1;
    repeat (9) tick();
    assert (value == 4'd9 && wrap)
      else $fatal(1, "counter did not indicate pending wrap at its bound");

    tick();
    assert (value == 4'd0 && !wrap)
      else $fatal(1, "counter did not wrap to zero");

    repeat (3) tick();
    enable = 1'b0;
    tick();
    assert (value == 4'd3 && !wrap)
      else $fatal(1, "counter did not resume and hold correctly");

    reset = 1'b1;
    tick();
    assert (value == 4'd0)
      else $fatal(1, "counter did not synchronously reset");

    $finish;
  end
endmodule
