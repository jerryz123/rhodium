// Simulates ordering and backpressure through a chained Queue and Pipe.
// SPDX-License-Identifier: Apache-2.0
module flow_chain_tb;
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

  BufferedLink dut (
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
    reset = 1'b0;

    ingress_in = '{valid: 1'b1, bits: 8'ha1};
    tick();
    ingress_in.valid = 1'b0;
    tick();
    tick();
    assert (egress_out.valid && egress_out.bits == 8'ha1)
      else $fatal(1, "chained flow did not deliver the first item");

    ingress_in = '{valid: 1'b1, bits: 8'hb2};
    tick();
    ingress_in.bits = 8'hc3;
    tick();
    ingress_in.valid = 1'b0;
    assert (egress_out.valid && egress_out.bits == 8'ha1)
      else $fatal(1, "chained flow did not hold under backpressure");

    egress_in.ready = 1'b1;
    tick();
    assert (egress_out.valid && egress_out.bits == 8'hb2)
      else $fatal(1, "chained flow reordered the second item");
    tick();
    assert (egress_out.valid && egress_out.bits == 8'hc3)
      else $fatal(1, "chained flow reordered the third item");
    tick();
    assert (!egress_out.valid)
      else $fatal(1, "chained flow did not drain");

    $finish;
  end
endmodule
