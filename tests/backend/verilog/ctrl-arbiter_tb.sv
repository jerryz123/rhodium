// Exercises fixed priority, chosen index, and selected token readiness.
// SPDX-License-Identifier: Apache-2.0
module ctrl_arbiter_tb;
  typedef struct packed { logic valid; } forward_t;
  typedef struct packed { logic ready; } reverse_t;

  forward_t ingress_0_in, ingress_1_in, ingress_2_in;
  reverse_t egress_in;
  reverse_t ingress_0_out, ingress_1_out, ingress_2_out;
  forward_t egress_out;
  logic [1:0] chosen;

  CtrlArbiter dut (.*);

  initial begin
    ingress_0_in = '{valid: 1'b0};
    ingress_1_in = '{valid: 1'b1};
    ingress_2_in = '{valid: 1'b1};
    egress_in = '{ready: 1'b1};
    #1;
    assert (egress_out.valid && chosen == 2'h1 &&
            !ingress_0_out.ready && ingress_1_out.ready &&
            !ingress_2_out.ready)
      else $fatal(1, "CtrlArbiter did not select the lowest valid index");

    ingress_0_in.valid = 1'b1;
    #1;
    assert (chosen == 2'h0 && ingress_0_out.ready &&
            !ingress_1_out.ready && !ingress_2_out.ready)
      else $fatal(1, "CtrlArbiter did not give input zero priority");

    egress_in.ready = 1'b0;
    #1;
    assert (egress_out.valid && !ingress_0_out.ready)
      else $fatal(1, "CtrlArbiter ignored output backpressure");
    $finish;
  end
endmodule
