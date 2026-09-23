// Checks materialized Queue bypass, backpressure, reset, wraparound, and simultaneous transfers.
// SPDX-License-Identifier: Apache-2.0
module materialized_queue_tb;
  typedef struct packed { logic valid; logic [7:0] bits; } forward_t;
  typedef struct packed { logic ready; } reverse_t;
  logic clock = 0;
  logic reset = 1;
  forward_t ingress_in;
  reverse_t egress_in;
  reverse_t ingress_out;
  forward_t egress_out;
  logic [1:0] occupancy;
  byte unsigned pending[$];
  int unsigned random_state = 32'h735a91bc;
  bit expected_ready;
  bit expected_valid;
  bit enqueue;
  bit dequeue;
  byte unsigned expected_bits;
  byte unsigned removed;

  MixedQueue dut(.*);

  task automatic check_outputs;
    expected_ready = pending.size() < 3 || egress_in.ready;
    expected_valid = pending.size() != 0 || ingress_in.valid;
    expected_bits = pending.size() == 0 ? ingress_in.bits : pending[0];
    assert (occupancy == 2'(pending.size()) && ingress_out.ready == expected_ready &&
            egress_out.valid == expected_valid && egress_out.bits == expected_bits)
      else $fatal(1, "materialized queue mismatch: count=%0d expected=%0d", occupancy, pending.size());
  endtask

  initial begin
    ingress_in = '0;
    egress_in = '0;
    #2;
    clock = 1;
    #2;
    clock = 0;
    reset = 0;
    for (int cycle = 0; cycle < 256; cycle++) begin
      random_state = random_state * 32'd1664525 + 32'd1013904223;
      ingress_in.valid = random_state[31];
      ingress_in.bits = random_state[15:8];
      egress_in.ready = random_state[30];
      // Directed fill/drain ensures both boundaries, then random traffic wraps pointers.
      if (cycle < 6) begin ingress_in.valid = 1; egress_in.ready = 0; end
      if (cycle >= 6 && cycle < 12) begin ingress_in.valid = 1; egress_in.ready = 1; end
      reset = cycle == 37 || cycle == 113;
      #2;
      check_outputs();
      enqueue = ingress_in.valid && expected_ready && !(pending.size() == 0 && egress_in.ready);
      dequeue = pending.size() != 0 && egress_in.ready;
      if (reset) pending.delete();
      else begin
        if (dequeue) removed = pending.pop_front();
        if (enqueue) pending.push_back(ingress_in.bits);
      end
      clock = 1;
      #2;
      check_outputs();
      clock = 0;
    end
    $display("materialized queue simulation passed");
    $finish;
  end
endmodule
