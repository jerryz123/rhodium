// Checks four committed stores, physical hazards, FIFO order, bounded age, and full replacement.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_store_buffer_tb;
  typedef struct packed {
    logic [63:0] address;
    logic way;
    logic [63:0] data;
    logic [7:0] mask;
    logic mark_dirty;
  } store_t;
  typedef struct packed {logic valid; store_t bits;} forward_t;
  typedef struct packed {logic ready;} backward_t;
  logic clock=0, reset=1;
  forward_t enqueue_in='0, drain_out;
  backward_t enqueue_out, drain_in='0;
  logic [63:0] query_address=0, probe_address=0;
  logic [7:0] query_mask=0;
  logic full_drain=0;
  logic word_hazard, line_hazard, queued_word_hazard, queued_line_hazard;
  logic empty, full, urgent;
  logic [2:0] count;
  RV5StageStoreBuffer dut(.*);
  always #5 clock=~clock;
  task automatic tick;
    @(posedge clock); #1;
    @(negedge clock); #1;
  endtask
  task automatic enqueue(input store_t store);
    enqueue_in='{valid:1,bits:store};
    #1;
    assert(enqueue_out.ready) else $fatal(1,"enqueue unexpectedly blocked");
    tick();
    enqueue_in='0;
  endtask
  initial begin
    tick(); reset=0;
    assert(empty && !full && enqueue_out.ready && count==0 && !drain_out.valid) else $fatal(1,"reset");
    enqueue_in='{valid:1,bits:'{address:64'h1000,way:0,data:64'h1122,mask:8'h03,mark_dirty:1}};
    query_address=64'h1001; query_mask=8'h02; probe_address=64'h1038;
    #1;
    assert(word_hazard && line_hazard && !queued_word_hazard && !queued_line_hazard) else $fatal(1,"same-cycle enqueue hazard missing");
    tick(); enqueue_in='0;
    enqueue('{address:64'h2000,way:1,data:64'h3344,mask:8'h0c,mark_dirty:0});
    enqueue('{address:64'h3000,way:0,data:64'h5566,mask:8'h30,mark_dirty:0});
    enqueue('{address:64'h4000,way:1,data:64'h7788,mask:8'hc0,mark_dirty:0});
    assert(count==4 && full && !enqueue_out.ready && !empty && drain_out.bits.address==64'h1000) else $fatal(1,"four-entry capacity");
    for(int entry=0;entry<4;entry++) begin
      query_address=64'h1000+(64'h1000*entry);
      query_mask=entry==0 ? 8'h03 : entry==1 ? 8'h0c : entry==2 ? 8'h30 : 8'hc0;
      probe_address=query_address+64'h38;
      #1;
      assert(word_hazard && queued_word_hazard && line_hazard && queued_line_hazard) else $fatal(1,"queued hazard missing");
      query_address+=8; query_mask=8'hff; #1;
      assert(!word_hazard) else $fatal(1,"different word blocked");
      query_address+=64'h100000; probe_address+=64'h100000; #1;
      assert(!word_hazard && !line_hazard) else $fatal(1,"same index different physical tag blocked");
    end
    repeat(10) tick();
    assert(urgent && drain_out.bits.data==64'h1122 && drain_out.bits.mark_dirty) else $fatal(1,"age or held payload");

    enqueue_in='{valid:1,bits:'{address:64'h5000,way:0,data:64'h99aa,mask:8'hff,mark_dirty:0}};
    drain_in.ready=1; full_drain=1; #1;
    assert(enqueue_out.ready) else $fatal(1,"full buffer did not accept draining replacement");
    tick(); enqueue_in='0; drain_in.ready=0; full_drain=0;
    assert(count==4 && full && drain_out.bits.address==64'h2000 && !urgent) else $fatal(1,"full replacement");

    drain_in.ready=1;
    assert(drain_out.bits.address==64'h2000); tick();
    assert(drain_out.bits.address==64'h3000); tick();
    assert(drain_out.bits.address==64'h4000); tick();
    assert(drain_out.bits.address==64'h5000); tick();
    assert(empty && !full && count==0 && !drain_out.valid) else $fatal(1,"final drain");
    $display("committed store buffer capacity, hazards, FIFO, age, and replacement passed");
    $finish;
  end
endmodule
