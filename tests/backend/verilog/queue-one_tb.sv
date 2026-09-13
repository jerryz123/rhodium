// Exercises default no-pipe behavior and occupancy reporting in a depth-one queue.
// SPDX-License-Identifier: Apache-2.0
module queue_one_tb;
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
  logic count;

  Queue dut (
    .clock       (clock),
    .reset       (reset),
    .ingress_in  (ingress_in),
    .egress_in   (egress_in),
    .ingress_out (ingress_out),
    .egress_out  (egress_out),
    .count       (count)
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
    reset = 1'b0;

    ingress_in = '{valid: 1'b1, bits: 8'ha1};
    tick();
    assert (egress_out.valid && egress_out.bits == 8'ha1 &&
            !ingress_out.ready && count)
      else $fatal(1, "depth-one queue did not become full");

    ingress_in.bits = 8'hb2;
    tick();
    assert (egress_out.bits == 8'ha1 && !ingress_out.ready)
      else $fatal(1, "stalled depth-one queue changed its payload");

    egress_in.ready = 1'b1;
    #1;
    assert (!ingress_out.ready && egress_out.bits == 8'ha1)
      else $fatal(1, "default depth-one queue unexpectedly enabled pipe");
    tick();
    assert (!egress_out.valid && ingress_out.ready && !count)
      else $fatal(1, "depth-one queue did not drain before replacement");

    tick();
    assert (egress_out.valid && egress_out.bits == 8'hb2 && count)
      else $fatal(1, "depth-one queue did not accept the delayed item");

    ingress_in.valid = 1'b0;
    tick();
    assert (!egress_out.valid && ingress_out.ready && !count)
      else $fatal(1, "depth-one queue did not drain");

    $finish;
  end
endmodule
