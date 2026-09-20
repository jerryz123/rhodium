// Checks qualified checkpoints on an unchanged pipeline with repeated data, bubbles, flush, and reset.
// SPDX-License-Identifier: Apache-2.0
module event_parents_tb;
  typedef struct packed {logic valid; logic [7:0] bits;} forward_t;
  logic clock=0,reset=1,flush=0;
  logic force_cache_event=0;
  forward_t source_in,wb_out,cache_out,both_out;
  EventQualifiedParents dut(.*);
  always #5 clock=~clock;
  import "DPI-C" function void parents_bind();
  import "DPI-C" function void parents_sample(int unsigned reset, flush, valid, data, wb_valid, wb_data, cache_valid, both_valid);
  import "DPI-C" function void parents_check();
  import "DPI-C" function void parents_finish();
  always @(posedge clock) begin
    parents_sample(int'(reset),int'(flush),int'(source_in.valid),int'(source_in.bits),int'(wb_out.valid),int'(wb_out.bits),int'(cache_out.valid),int'(both_out.valid));
    if(!reset && (cache_out.bits!=wb_out.bits || both_out.bits!=wb_out.bits)) $fatal(1,"branch payload mismatch");
    #1; parents_check();
  end
  task automatic tick; @(posedge clock); #2; endtask
  int unsigned rng=32'hc7651234;
  function automatic int unsigned random_word();
    rng^=rng<<13; rng^=rng>>17; rng^=rng<<5; return rng;
  endfunction
  initial begin
    parents_bind(); source_in='0; tick(); reset=0;
    for(int i=0;i<1000;i++) begin
      automatic int unsigned r=random_word();
      reset=(i%223)==222; flush=r[4:2]==0;
      source_in.valid=r[0]; source_in.bits=r[1]?8'h55:8'h54; tick();
    end
    reset=0; flush=0; source_in.valid=0; repeat(2) tick();
    parents_finish(); $finish;
  end
  initial begin #20000; $fatal(1,"selected parent timeout"); end
endmodule
