// Exercises fixed priority, selected readiness, chosen index, and stalled preemption.
// SPDX-License-Identifier: Apache-2.0
module arbiter_tb;
  typedef struct packed {
    logic       valid;
    logic [7:0] bits;
  } forward_t;
  typedef struct packed {
    logic ready;
  } reverse_t;

  forward_t ingress_0_in;
  forward_t ingress_1_in;
  forward_t ingress_2_in;
  reverse_t egress_in;
  reverse_t ingress_0_out;
  reverse_t ingress_1_out;
  reverse_t ingress_2_out;
  forward_t egress_out;
  logic [1:0] chosen;

  Arbiter dut (
    .ingress_0_in  (ingress_0_in),
    .ingress_1_in  (ingress_1_in),
    .ingress_2_in  (ingress_2_in),
    .egress_in     (egress_in),
    .ingress_0_out (ingress_0_out),
    .ingress_1_out (ingress_1_out),
    .ingress_2_out (ingress_2_out),
    .egress_out    (egress_out),
    .chosen        (chosen)
  );

  initial begin
    ingress_0_in = '{valid: 1'b0, bits: 8'ha0};
    ingress_1_in = '{valid: 1'b0, bits: 8'hb1};
    ingress_2_in = '{valid: 1'b0, bits: 8'hc2};
    egress_in = '{ready: 1'b0};
    #1;
    assert (!egress_out.valid && chosen == 2'h0 &&
            !ingress_0_out.ready && !ingress_1_out.ready &&
            !ingress_2_out.ready)
      else $fatal(1, "arbiter no-request behavior is incorrect");

    ingress_2_in.valid = 1'b1;
    #1;
    assert (egress_out.valid && egress_out.bits == 8'hc2 && chosen == 2'h2)
      else $fatal(1, "arbiter did not select input two");

    ingress_1_in.valid = 1'b1;
    #1;
    assert (egress_out.bits == 8'hb1 && chosen == 2'h1)
      else $fatal(1, "input one did not preempt input two");

    ingress_0_in.valid = 1'b1;
    egress_in.ready = 1'b1;
    #1;
    assert (egress_out.bits == 8'ha0 && chosen == 2'h0 &&
            ingress_0_out.ready && !ingress_1_out.ready &&
            !ingress_2_out.ready)
      else $fatal(1, "arbiter priority or selected readiness is incorrect");

    ingress_0_in.valid = 1'b0;
    #1;
    assert (egress_out.bits == 8'hb1 && chosen == 2'h1 &&
            ingress_1_out.ready && !ingress_2_out.ready)
      else $fatal(1, "arbiter did not advance to input one");

    egress_in.ready = 1'b0;
    ingress_0_in.valid = 1'b1;
    #1;
    assert (egress_out.bits == 8'ha0 && chosen == 2'h0 &&
            !ingress_0_out.ready)
      else $fatal(1, "stalled Decoupled output did not permit preemption");

    $finish;
  end
endmodule
