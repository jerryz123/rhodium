// Verifies payload-selected atomic transfer, changing stalled offers, and empty selection.
module selective_atomic_fork_tb;
  typedef struct packed {
    logic [2:0] destinations;
    logic [7:0] data;
  } payload_t;
  typedef struct packed {
    logic     valid;
    payload_t bits;
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

  SelectiveFanout dut (
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
    ingress_in = '{valid: 1'b1, bits: '{destinations: 3'b101, data: 8'h5a}};
    egress_0_in = '{ready: 1'b1};
    egress_1_in = '{ready: 1'b0};
    egress_2_in = '{ready: 1'b0};
    #1;
    assert (!ingress_out.ready && !egress_0_out.valid &&
            !egress_1_out.valid && egress_2_out.valid)
      else $fatal(1, "selective fork allowed a partial transfer");

    egress_2_in.ready = 1'b1;
    #1;
    assert (ingress_out.ready && egress_0_out.valid &&
            !egress_1_out.valid && egress_2_out.valid &&
            egress_0_out.bits.data == 8'h5a && egress_2_out.bits.data == 8'h5a)
      else $fatal(1, "selective fork did not release selected outputs together");

    egress_0_in.ready = 1'b0;
    egress_1_in.ready = 1'b1;
    egress_2_in.ready = 1'b0;
    ingress_in.bits = '{destinations: 3'b011, data: 8'ha6};
    #1;
    assert (!ingress_out.ready && egress_0_out.valid &&
            !egress_1_out.valid && !egress_2_out.valid &&
            egress_0_out.bits.data == 8'ha6)
      else $fatal(1, "selective fork did not track a changed Decoupled offer");

    ingress_in.bits.destinations = 3'b000;
    #1;
    assert (ingress_out.ready && !egress_0_out.valid &&
            !egress_1_out.valid && !egress_2_out.valid)
      else $fatal(1, "empty selection did not consume without fanout");

    ingress_in.valid = 1'b0;
    #1;
    assert (!egress_0_out.valid && !egress_1_out.valid && !egress_2_out.valid)
      else $fatal(1, "selective fork manufactured an output offer");

    $finish;
  end
endmodule
