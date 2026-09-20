// Exercises latency, throughput, and backpressure stability in a two-stage pipe.
// SPDX-License-Identifier: Apache-2.0
module pipe_tb;
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
  reverse_t egress_in;
  reverse_t ingress_out;
  forward_t egress_out;

  Pipe dut (
    .clock       (clock),
    .reset       (reset),
    .ingress_in  (ingress_in),
    .egress_in   (egress_in),
    .ingress_out (ingress_out),
    .egress_out  (egress_out)
  );

  always #5 clock = ~clock;

  task automatic tick;
    @(posedge clock);
    #1;
  endtask

  initial begin
    ingress_in = '{valid: 1'b0, bits: 8'h00};
    egress_in = '{ready: 1'b0};
    tick();
    assert (!egress_out.valid && ingress_out.ready)
      else $fatal(1, "pipe reset did not empty both stages");

    reset = 1'b0;
    egress_in.ready = 1'b1;
    ingress_in = '{valid: 1'b1, bits: 8'ha1};
    tick();
    assert (!egress_out.valid)
      else $fatal(1, "two-stage pipe produced data one cycle too early");

    ingress_in.bits = 8'hb2;
    tick();
    assert (egress_out.valid && egress_out.bits == 8'ha1)
      else $fatal(1, "two-stage pipe latency is incorrect");

    egress_in.ready = 1'b0;
    ingress_in.bits = 8'hc3;
    #1;
    assert (!ingress_out.ready)
      else $fatal(1, "full stalled pipe accepted an item");
    tick();
    assert (egress_out.valid && egress_out.bits == 8'ha1)
      else $fatal(1, "stalled pipe did not hold its output stable");

    egress_in.ready = 1'b1;
    #1;
    assert (ingress_out.ready)
      else $fatal(1, "draining pipe did not accept a replacement item");
    tick();
    assert (egress_out.valid && egress_out.bits == 8'hb2)
      else $fatal(1, "pipe replacement lost the second item");

    ingress_in.valid = 1'b0;
    tick();
    assert (egress_out.valid && egress_out.bits == 8'hc3)
      else $fatal(1, "pipe replacement lost the third item");
    tick();
    assert (!egress_out.valid)
      else $fatal(1, "pipe did not become empty");

    $finish;
  end
endmodule
