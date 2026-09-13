// Verifies that a join never partially consumes its ready-valid inputs.
// SPDX-License-Identifier: Apache-2.0
module join_tb;
  typedef struct packed {
    logic       valid;
    logic [7:0] bits;
  } forward_t;
  typedef struct packed {
    logic ready;
  } reverse_t;
  typedef struct packed {
    logic             valid;
    logic [2:0][7:0] bits;
  } joined_forward_t;

  forward_t ingress_0_in;
  forward_t ingress_1_in;
  forward_t ingress_2_in;
  reverse_t egress_in;
  reverse_t ingress_0_out;
  reverse_t ingress_1_out;
  reverse_t ingress_2_out;
  joined_forward_t egress_out;

  Join dut (
    .ingress_0_in  (ingress_0_in),
    .ingress_1_in  (ingress_1_in),
    .ingress_2_in  (ingress_2_in),
    .egress_in     (egress_in),
    .ingress_0_out (ingress_0_out),
    .ingress_1_out (ingress_1_out),
    .ingress_2_out (ingress_2_out),
    .egress_out    (egress_out)
  );

  initial begin
    ingress_0_in = '{valid: 1'b1, bits: 8'ha0};
    ingress_1_in = '{valid: 1'b1, bits: 8'hb1};
    ingress_2_in = '{valid: 1'b0, bits: 8'hc2};
    egress_in = '{ready: 1'b1};
    #1;
    assert (!egress_out.valid && !ingress_0_out.ready &&
            !ingress_1_out.ready && ingress_2_out.ready)
      else $fatal(1, "join partially enabled a valid input");

    ingress_2_in.valid = 1'b1;
    egress_in.ready = 1'b0;
    #1;
    assert (egress_out.valid && !ingress_0_out.ready &&
            !ingress_1_out.ready && !ingress_2_out.ready &&
            egress_out.bits[0] == 8'ha0 &&
            egress_out.bits[1] == 8'hb1 &&
            egress_out.bits[2] == 8'hc2)
      else $fatal(1, "join payload or output backpressure is incorrect");

    egress_in.ready = 1'b1;
    #1;
    assert (ingress_0_out.ready && ingress_1_out.ready &&
            ingress_2_out.ready)
      else $fatal(1, "join did not release all inputs atomically");

    $finish;
  end
endmodule
