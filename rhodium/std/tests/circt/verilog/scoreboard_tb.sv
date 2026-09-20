// Verifies registered non-power-of-two scoreboard set, clear, and retention behavior.
// SPDX-License-Identifier: Apache-2.0
module scoreboard_tb;
  typedef struct packed { logic valid; logic [1:0] bits; } update_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  update_t set_in;
  update_t clear_in;
  logic [2:0] busy;

  Scoreboard dut (.*);
  always #5 clock = ~clock;

  initial begin
    set_in = '0;
    clear_in = '0;
    repeat (2) @(posedge clock);
    #1;
    reset = 1'b0;

    set_in.valid = 1'b1;
    set_in.bits = 2'd2;
    #1;
    assert (busy == 3'b000)
      else $fatal(1, "set changed registered occupancy before the edge");
    @(posedge clock);
    #1;
    set_in.valid = 1'b0;
    assert (busy == 3'b100)
      else $fatal(1, "set entry was not retained");

    clear_in.valid = 1'b1;
    clear_in.bits = 2'd2;
    #1;
    assert (busy == 3'b100)
      else $fatal(1, "clear changed registered occupancy before the edge");
    @(posedge clock);
    #1;
    clear_in.valid = 1'b0;
    assert (busy == 3'b000)
      else $fatal(1, "cleared entry remained busy");

    set_in.valid = 1'b1;
    set_in.bits = 2'd0;
    #1;
    assert (busy == 3'b000)
      else $fatal(1, "entry-zero set changed occupancy before the edge");
    @(posedge clock);
    #1;
    set_in.bits = 2'd1;
    clear_in.valid = 1'b1;
    clear_in.bits = 2'd1;
    assert (busy == 3'b001)
      else $fatal(1, "same-cycle clear did not win");
    @(posedge clock);
    #1;
    set_in.valid = 1'b0;
    clear_in.bits = 2'd0;
    #1;
    assert (busy == 3'b001)
      else $fatal(1, "entry-zero clear changed occupancy before the edge");
    @(posedge clock);
    #1;
    clear_in.valid = 1'b0;
    assert (busy == 3'b000)
      else $fatal(1, "entry zero clear was not retained");

    $display("standard scoreboard passed");
    $finish;
  end
endmodule
