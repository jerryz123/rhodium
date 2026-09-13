// Verifies packet ownership across transfers, bubbles, stalls, and final flits.
// SPDX-License-Identifier: Apache-2.0
module packet_rr_arbiter_tb;
  typedef struct packed {
    logic       head;
    logic       tail;
    logic [7:0] payload;
  } flit_t;
  typedef struct packed {
    logic  valid;
    flit_t bits;
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

  PacketArbitration dut (.*);

  always #5 clock = ~clock;

  task automatic tick;
    @(posedge clock);
    #1;
  endtask

  initial begin
    ingress_0_in = '{valid: 1'b1, bits: '{head: 1'b1, tail: 1'b0, payload: 8'ha0}};
    ingress_1_in = '{valid: 1'b1, bits: '{head: 1'b1, tail: 1'b1, payload: 8'hb0}};
    ingress_2_in = '{valid: 1'b1, bits: '{head: 1'b1, tail: 1'b1, payload: 8'hc0}};
    egress_in = '{ready: 1'b1};
    tick();
    reset = 1'b0;

    #1;
    assert (egress_out.valid && egress_out.bits.payload == 8'ha0 &&
            ingress_0_out.ready && !ingress_1_out.ready && !ingress_2_out.ready)
      else $fatal(1, "packet arbiter did not initially select input zero");
    tick();

    ingress_0_in.valid = 1'b0;
    #1;
    assert (!egress_out.valid && ingress_0_out.ready &&
            !ingress_1_out.ready && !ingress_2_out.ready)
      else $fatal(1, "packet arbiter did not retain ownership across a bubble");
    tick();

    ingress_0_in = '{valid: 1'b1, bits: '{head: 1'b0, tail: 1'b0, payload: 8'ha1}};
    tick();
    assert (egress_out.valid && egress_out.bits.payload == 8'ha1 &&
            ingress_0_out.ready)
      else $fatal(1, "packet arbiter changed owner before the final flit");

    ingress_0_in.bits = '{head: 1'b0, tail: 1'b1, payload: 8'ha2};
    egress_in.ready = 1'b0;
    #1;
    assert (egress_out.valid && egress_out.bits.payload == 8'ha2 &&
            !ingress_0_out.ready && !ingress_1_out.ready)
      else $fatal(1, "packet arbiter did not retain its stalled final flit");
    tick();

    egress_in.ready = 1'b1;
    tick();
    assert (egress_out.valid && egress_out.bits.payload == 8'hb0 &&
            ingress_1_out.ready && !ingress_0_out.ready)
      else $fatal(1, "packet arbiter did not rotate after packet completion");
    tick();
    assert (egress_out.valid && egress_out.bits.payload == 8'hc0 &&
            ingress_2_out.ready)
      else $fatal(1, "single-flit packet did not release ownership");

    $finish;
  end
endmodule
