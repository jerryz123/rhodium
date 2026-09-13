// Requires a missing-parent assertion when a child observes a suppressed upstream checkpoint.
// SPDX-License-Identifier: Apache-2.0
module event_parents_missing_tb;
  typedef struct packed {logic valid; logic [7:0] bits;} forward_t;
  logic clock=0, reset=1, flush=0, force_cache_event=1;
  forward_t source_in, wb_out, cache_out, both_out;
  EventQualifiedParents dut(.*);
  always #5 clock=~clock;
  initial begin
    source_in='0;
    repeat(2) @(negedge clock);
    reset=0;
    source_in.valid=1;
    source_in.bits=8'h54; // WB exists, but the intermediate checkpoint does not.
    repeat(3) @(negedge clock);
    $finish;
  end
endmodule
