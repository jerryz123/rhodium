// Exercises rotating fairness and selected readiness in the token arbiter.
// SPDX-License-Identifier: Apache-2.0
module ctrl_rr_arbiter_tb;
  typedef struct packed { logic valid; } forward_t;
  typedef struct packed { logic ready; } reverse_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  forward_t ingress_0_in, ingress_1_in, ingress_2_in;
  reverse_t egress_in;
  reverse_t ingress_0_out, ingress_1_out, ingress_2_out;
  forward_t egress_out;
  logic [1:0] chosen;

  CtrlRRArbiter dut (.*);
  always #5 clock = ~clock;
  task automatic tick; @(posedge clock); #1; endtask

  initial begin
    ingress_0_in = '{valid: 1'b1};
    ingress_1_in = '{valid: 1'b1};
    ingress_2_in = '{valid: 1'b1};
    egress_in = '{ready: 1'b0};
    tick();
    reset = 1'b0;
    assert (chosen == 2'h0 && !ingress_0_out.ready)
      else $fatal(1, "CtrlRRArbiter did not start at input zero");

    egress_in.ready = 1'b1;
    tick();
    assert (chosen == 2'h1 && ingress_1_out.ready)
      else $fatal(1, "CtrlRRArbiter did not advance to input one");
    tick();
    assert (chosen == 2'h2 && ingress_2_out.ready)
      else $fatal(1, "CtrlRRArbiter did not advance to input two");
    tick();
    assert (chosen == 2'h0 && ingress_0_out.ready)
      else $fatal(1, "CtrlRRArbiter did not wrap to input zero");
    $finish;
  end
endmodule
