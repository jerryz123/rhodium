// Exercises flow-through and piped full replacement in a token queue.
// SPDX-License-Identifier: Apache-2.0
module ctrl_queue_options_tb;
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
    reset = 1'b0;
    assert (count == 2'h0 && !egress_out.valid)
      else $fatal(1, "configured CtrlQueue did not reset empty");

    ingress_in.valid = 1'b1;
    egress_in.ready = 1'b1;
    #1;
    assert (ingress_out.ready && egress_out.valid && count == 2'h0)
      else $fatal(1, "flow option did not provide empty token bypass");
    tick();
    ingress_in.valid = 1'b0;
    #1;
    assert (count == 2'h0 && !egress_out.valid)
      else $fatal(1, "flow-through token was incorrectly stored");

    egress_in.ready = 1'b0;
    ingress_in.valid = 1'b1;
    tick();
    assert (count == 2'h1 && egress_out.valid)
      else $fatal(1, "stalled flow token was not stored");
    tick();
    assert (count == 2'h2 && !ingress_out.ready)
      else $fatal(1, "configured CtrlQueue did not become full");

    egress_in.ready = 1'b1;
    #1;
    assert (ingress_out.ready && egress_out.valid)
      else $fatal(1, "pipe option did not permit full token replacement");
    tick();
    assert (count == 2'h2 && egress_out.valid)
      else $fatal(1, "piped token replacement changed occupancy");
    $finish;
  end
endmodule
