// Exercises token FIFO capacity, count, full backpressure, and draining.
// SPDX-License-Identifier: Apache-2.0
module ctrl_queue_tb;
  typedef struct packed { logic valid; } forward_t;
  typedef struct packed { logic ready; } reverse_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  forward_t ingress_in;
  reverse_t egress_in;
  reverse_t ingress_out;
  forward_t egress_out;
  logic [1:0] count;

  CtrlQueue dut (.*);
  always #5 clock = ~clock;
  task automatic tick; @(posedge clock); #1; endtask

  initial begin
    ingress_in = '{valid: 1'b0};
    egress_in = '{ready: 1'b0};
    tick();
    assert (!egress_out.valid && ingress_out.ready && count == 2'h0)
      else $fatal(1, "CtrlQueue reset was not empty");

    reset = 1'b0;
    ingress_in.valid = 1'b1;
    repeat (3) begin
      assert (ingress_out.ready)
        else $fatal(1, "CtrlQueue rejected a token before becoming full");
      tick();
    end
    assert (egress_out.valid && !ingress_out.ready && count == 2'h3)
      else $fatal(1, "CtrlQueue did not reach full capacity");
    tick();
    assert (count == 2'h3)
      else $fatal(1, "stalled full CtrlQueue changed occupancy");

    ingress_in.valid = 1'b0;
    egress_in.ready = 1'b1;
    repeat (3) tick();
    assert (!egress_out.valid && ingress_out.ready && count == 2'h0)
      else $fatal(1, "CtrlQueue did not drain every token");
    $finish;
  end
endmodule
