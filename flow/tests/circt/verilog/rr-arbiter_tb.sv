// Verifies rotating fairness, selected readiness, and legal stalled reselection.
// SPDX-License-Identifier: Apache-2.0
module rr_arbiter_tb;
  typedef struct packed {
    logic       valid;
    logic [7:0] bits;
  } forward_t;
  typedef struct packed {
    logic ready;
  } reverse_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  forward_t ingress_0_in;
  forward_t ingress_1_in;
  forward_t ingress_2_in;
  reverse_t egress_in;
  reverse_t ingress_0_out;
  reverse_t ingress_1_out;
  reverse_t ingress_2_out;
  forward_t egress_out;
  logic [1:0] chosen;

  RRArbiter dut (
    .clock         (clock),
    .reset         (reset),
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

  always #5 clock = ~clock;

  task automatic tick;
    @(posedge clock);
    #1;
  endtask

  initial begin
    ingress_0_in = '{valid: 1'b0, bits: 8'ha0};
    ingress_1_in = '{valid: 1'b0, bits: 8'hb1};
    ingress_2_in = '{valid: 1'b0, bits: 8'hc2};
    egress_in = '{ready: 1'b0};
    tick();
    reset = 1'b0;

    ingress_0_in.valid = 1'b1;
    ingress_1_in.valid = 1'b1;
    ingress_2_in.valid = 1'b1;
    #1;
    assert (chosen == 2'h0 && egress_out.bits == 8'ha0 &&
            !ingress_0_out.ready)
      else $fatal(1, "round-robin arbiter did not start at input zero");

    egress_in.ready = 1'b1;
    tick();
    assert (chosen == 2'h1 && egress_out.bits == 8'hb1 &&
            ingress_1_out.ready && !ingress_0_out.ready &&
            !ingress_2_out.ready)
      else $fatal(1, "round-robin arbiter did not advance to input one");
    tick();
    assert (chosen == 2'h2 && egress_out.bits == 8'hc2 &&
            ingress_2_out.ready)
      else $fatal(1, "round-robin arbiter did not advance to input two");
    tick();
    assert (chosen == 2'h0 && egress_out.bits == 8'ha0)
      else $fatal(1, "round-robin arbiter did not wrap to input zero");

    egress_in.ready = 1'b0;
    ingress_0_in.valid = 1'b0;
    #1;
    assert (chosen == 2'h1 && egress_out.bits == 8'hb1 &&
            !ingress_1_out.ready)
      else $fatal(1, "stalled Decoupled output did not permit reselection");

    $finish;
  end
endmodule
