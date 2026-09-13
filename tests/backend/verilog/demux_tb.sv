// Verifies selected routing, selected backpressure, and invalid-selector blocking.
// SPDX-License-Identifier: Apache-2.0
module demux_tb;
  typedef struct packed {
    logic       valid;
    logic [7:0] bits;
  } forward_t;
  typedef struct packed {
    logic ready;
  } reverse_t;

  forward_t ingress_in;
  logic [1:0] select;
  reverse_t egress_0_in;
  reverse_t egress_1_in;
  reverse_t egress_2_in;
  reverse_t ingress_out;
  forward_t egress_0_out;
  forward_t egress_1_out;
  forward_t egress_2_out;

  Demux dut (
    .ingress_in  (ingress_in),
    .select      (select),
    .egress_0_in (egress_0_in),
    .egress_1_in (egress_1_in),
    .egress_2_in (egress_2_in),
    .ingress_out (ingress_out),
    .egress_0_out(egress_0_out),
    .egress_1_out(egress_1_out),
    .egress_2_out(egress_2_out)
  );

  initial begin
    ingress_in = '{valid: 1'b1, bits: 8'h5a};
    egress_0_in = '{ready: 1'b1};
    egress_1_in = '{ready: 1'b0};
    egress_2_in = '{ready: 1'b1};

    select = 2'h0;
    #1;
    assert (ingress_out.ready && egress_0_out.valid &&
            !egress_1_out.valid && !egress_2_out.valid &&
            egress_0_out.bits == 8'h5a)
      else $fatal(1, "demux did not route to output zero");

    select = 2'h1;
    #1;
    assert (!ingress_out.ready && egress_1_out.valid &&
            !egress_0_out.valid && !egress_2_out.valid)
      else $fatal(1, "demux did not propagate selected backpressure");

    select = 2'h2;
    #1;
    assert (ingress_out.ready && egress_2_out.valid &&
            egress_2_out.bits == 8'h5a)
      else $fatal(1, "demux did not route to output two");

    select = 2'h3;
    #1;
    assert (!ingress_out.ready && !egress_0_out.valid &&
            !egress_1_out.valid && !egress_2_out.valid)
      else $fatal(1, "out-of-range demux selection did not block");

    $finish;
  end
endmodule
