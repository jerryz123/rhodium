// Verifies exactly-once broadcast delivery under independent output stalls.
// SPDX-License-Identifier: Apache-2.0
module broadcast_tb;
  typedef struct packed {
    logic       valid;
    logic [7:0] bits;
  } forward_t;
  typedef struct packed {
    logic ready;
  } reverse_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  forward_t ingress_in;
  reverse_t egress_0_in;
  reverse_t egress_1_in;
  reverse_t egress_2_in;
  reverse_t ingress_out;
  forward_t egress_0_out;
  forward_t egress_1_out;
  forward_t egress_2_out;

  Broadcast dut (
    .clock        (clock),
    .reset        (reset),
    .ingress_in   (ingress_in),
    .egress_0_in  (egress_0_in),
    .egress_1_in  (egress_1_in),
    .egress_2_in  (egress_2_in),
    .ingress_out  (ingress_out),
    .egress_0_out (egress_0_out),
    .egress_1_out (egress_1_out),
    .egress_2_out (egress_2_out)
  );

  always #5 clock = ~clock;

  task automatic tick;
    @(posedge clock);
    #1;
  endtask

  initial begin
    ingress_in = '{valid: 1'b0, bits: 8'h00};
    egress_0_in = '{ready: 1'b0};
    egress_1_in = '{ready: 1'b0};
    egress_2_in = '{ready: 1'b0};
    tick();
    reset = 1'b0;

    ingress_in = '{valid: 1'b1, bits: 8'ha5};
    assert (ingress_out.ready)
      else $fatal(1, "empty broadcast did not accept input");
    tick();
    ingress_in.valid = 1'b0;
    assert (egress_0_out.valid && egress_1_out.valid &&
            egress_2_out.valid && egress_0_out.bits == 8'ha5 &&
            !ingress_out.ready)
      else $fatal(1, "broadcast did not present the captured item");

    egress_0_in.ready = 1'b1;
    tick();
    assert (!egress_0_out.valid && egress_1_out.valid &&
            egress_2_out.valid && egress_1_out.bits == 8'ha5)
      else $fatal(1, "broadcast redelivered or lost the first recipient");

    egress_2_in.ready = 1'b1;
    tick();
    assert (!egress_0_out.valid && egress_1_out.valid &&
            !egress_2_out.valid && egress_1_out.bits == 8'ha5)
      else $fatal(1, "broadcast redelivered or lost the third recipient");

    ingress_in = '{valid: 1'b1, bits: 8'h3c};
    egress_1_in.ready = 1'b1;
    #1;
    assert (ingress_out.ready)
      else $fatal(1, "broadcast could not refill on the final drain");
    tick();
    ingress_in.valid = 1'b0;
    assert (egress_0_out.valid && egress_1_out.valid &&
            egress_2_out.valid && egress_0_out.bits == 8'h3c &&
            egress_1_out.bits == 8'h3c && egress_2_out.bits == 8'h3c)
      else $fatal(1, "broadcast refill did not reach every recipient");

    $finish;
  end
endmodule
