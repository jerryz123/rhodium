// Verifies that a token join never partially consumes its inputs.
// SPDX-License-Identifier: Apache-2.0
module ctrl_join_tb;
  typedef struct packed { logic valid; } forward_t;
  typedef struct packed { logic ready; } reverse_t;

  forward_t ingress_0_in, ingress_1_in, ingress_2_in;
  reverse_t egress_in;
  reverse_t ingress_0_out, ingress_1_out, ingress_2_out;
  forward_t egress_out;

  CtrlJoin dut (.*);

  initial begin
    ingress_0_in = '{valid: 1'b1};
    ingress_1_in = '{valid: 1'b1};
    ingress_2_in = '{valid: 1'b0};
    egress_in = '{ready: 1'b1};
    #1;
    assert (!egress_out.valid && !ingress_0_out.ready &&
            !ingress_1_out.ready && ingress_2_out.ready)
      else $fatal(1, "CtrlJoin partially enabled a valid input");

    ingress_2_in.valid = 1'b1;
    egress_in.ready = 1'b0;
    #1;
    assert (egress_out.valid && !ingress_0_out.ready &&
            !ingress_1_out.ready && !ingress_2_out.ready)
      else $fatal(1, "CtrlJoin ignored output backpressure");
    egress_in.ready = 1'b1;
    #1;
    assert (ingress_0_out.ready && ingress_1_out.ready &&
            ingress_2_out.ready)
      else $fatal(1, "CtrlJoin did not release every input atomically");
    $finish;
  end
endmodule
