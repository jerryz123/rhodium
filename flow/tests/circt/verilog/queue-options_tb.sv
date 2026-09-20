// Exercises flow-through, piped full replacement, and count queue options.
// SPDX-License-Identifier: Apache-2.0
module queue_options_tb;
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

  initial begin
    ingress_in = '{valid: 1'b0, bits: 8'h00};
    egress_in = '{ready: 1'b0};
    tick();
    reset = 1'b0;
    assert (count == 2'h0 && !egress_out.valid)
      else $fatal(1, "configured queue did not reset empty");

    ingress_in = '{valid: 1'b1, bits: 8'ha1};
    egress_in.ready = 1'b1;
    #1;
    assert (ingress_out.ready && egress_out.valid &&
            egress_out.bits == 8'ha1 && count == 2'h0)
      else $fatal(1, "flow option did not provide empty bypass");
    tick();
    ingress_in.valid = 1'b0;
    #1;
    assert (count == 2'h0 && !egress_out.valid)
      else $fatal(1, "flow-through transfer was incorrectly stored");

    egress_in.ready = 1'b0;
    ingress_in = '{valid: 1'b1, bits: 8'hb2};
    tick();
    assert (count == 2'h1 && egress_out.valid && egress_out.bits == 8'hb2)
      else $fatal(1, "stalled flow input was not stored");
    ingress_in.bits = 8'hc3;
    tick();
    assert (count == 2'h2 && !ingress_out.ready)
      else $fatal(1, "configured queue did not become full");

    ingress_in.bits = 8'hd4;
    egress_in.ready = 1'b1;
    #1;
    assert (ingress_out.ready && egress_out.bits == 8'hb2)
      else $fatal(1, "pipe option did not permit full replacement");
    tick();
    assert (count == 2'h2 && egress_out.bits == 8'hc3)
      else $fatal(1, "piped replacement changed occupancy or order");

    $finish;
  end
endmodule
