// Exercises default FIFO order, full backpressure, count, wraparound, and stalls.
// SPDX-License-Identifier: Apache-2.0
module queue_tb;
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
  logic [1:0] count;

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

  task automatic enqueue(input logic [7:0] data);
    ingress_in = '{valid: 1'b1, bits: data};
    #1;
    assert (ingress_out.ready)
      else $fatal(1, "queue unexpectedly rejected an enqueue");
    tick();
  endtask

  initial begin
    ingress_in = '{valid: 1'b0, bits: 8'h00};
    egress_in = '{ready: 1'b0};
    tick();
    assert (!egress_out.valid && ingress_out.ready && count == 2'h0)
      else $fatal(1, "queue reset did not produce an empty queue");

    reset = 1'b0;
    enqueue(8'h11);
    assert (egress_out.valid && egress_out.bits == 8'h11)
      else $fatal(1, "queue head was not visible after enqueue");
    enqueue(8'h22);
    enqueue(8'h33);
    #1;
    assert (!ingress_out.ready && egress_out.bits == 8'h11 && count == 2'h3)
      else $fatal(1, "queue did not apply full backpressure");

    ingress_in = '{valid: 1'b1, bits: 8'h44};
    tick();
    assert (!ingress_out.ready && egress_out.bits == 8'h11 && count == 2'h3)
      else $fatal(1, "stalled full queue changed state");

    egress_in.ready = 1'b1;
    #1;
    assert (!ingress_out.ready && egress_out.bits == 8'h11)
      else $fatal(1, "default queue unexpectedly enabled piped replacement");
    tick();
    assert (egress_out.valid && egress_out.bits == 8'h22 &&
            ingress_out.ready && count == 2'h2)
      else $fatal(1, "queue did not free capacity after dequeue");

    tick();
    assert (egress_out.valid && egress_out.bits == 8'h33 && count == 2'h2)
      else $fatal(1, "queue lost FIFO order while accepting the delayed item");

    ingress_in.valid = 1'b0;
    tick();
    assert (egress_out.valid && egress_out.bits == 8'h44)
      else $fatal(1, "queue wraparound lost the replacement item");
    tick();
    assert (!egress_out.valid && ingress_out.ready && count == 2'h0)
      else $fatal(1, "queue did not return to empty");

    $finish;
  end
endmodule
