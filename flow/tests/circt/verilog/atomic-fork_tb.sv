// Verifies that an atomic fork never permits a partial ready-valid transfer.
// SPDX-License-Identifier: Apache-2.0
module atomic_fork_tb;
  typedef struct packed {
    logic       valid;
    logic [7:0] bits;
  } forward_t;
  typedef struct packed {
    logic ready;
  } reverse_t;

  forward_t ingress_in;
  reverse_t egress_0_in;
  reverse_t egress_1_in;
  reverse_t egress_2_in;
  reverse_t ingress_out;
  forward_t egress_0_out;
  forward_t egress_1_out;
  forward_t egress_2_out;

  AtomicFork dut (
    .ingress_in  (ingress_in),
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
    #1;
    assert (!ingress_out.ready &&
            !(egress_0_out.valid && egress_0_in.ready) &&
            egress_1_out.valid &&
            !(egress_2_out.valid && egress_2_in.ready))
      else $fatal(1, "atomic fork allowed a partial transfer");

    egress_1_in.ready = 1'b1;
    #1;
    assert (ingress_out.ready && egress_0_out.valid &&
            egress_1_out.valid && egress_2_out.valid &&
            egress_0_out.bits == 8'h5a &&
            egress_1_out.bits == 8'h5a &&
            egress_2_out.bits == 8'h5a)
      else $fatal(1, "atomic fork did not release every output together");

    ingress_in.valid = 1'b0;
    #1;
    assert (!egress_0_out.valid && !egress_1_out.valid &&
            !egress_2_out.valid)
      else $fatal(1, "atomic fork manufactured an output offer");

    $finish;
  end
endmodule
