// Checks two committed entries, all-byte hazards, same-cycle enqueue, FIFO order, and bounded age.
module rv5stage_pending_stores_tb;
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
  logic word_hazard, line_hazard, empty, urgent;
  logic [1:0] count;
  RV5StagePendingStores dut(.*);
  always #5 clock=~clock;
  task automatic tick;
    @(posedge clock); #1;
    @(negedge clock); #1;
  endtask
  initial begin
    tick(); reset=0;
    assert(empty && enqueue_out.ready && count==0 && !drain_out.valid) else $fatal(1,"reset");
    enqueue_in='{valid:1,bits:'{address:64'h1000,way:0,data:64'h1122,mask:8'h03,mark_dirty:1}};
    query_address=64'h1001; query_mask=8'h02; probe_address=64'h1038;
    #1;
    assert(word_hazard && line_hazard) else $fatal(1,"same-cycle enqueue hazard missing");
    tick();
    enqueue_in='{valid:1,bits:'{address:64'h2000,way:1,data:64'h3344,mask:8'h0c,mark_dirty:0}};
    tick(); enqueue_in='0;
    assert(count==2 && !enqueue_out.ready && !empty && drain_out.bits.address==64'h1000) else $fatal(1,"two-entry capacity");
    for(int entry=0;entry<2;entry++) begin
      query_address=entry==0 ? 64'h1000 : 64'h2000;
      for(int lane=0;lane<8;lane++) begin
        query_mask=8'(1<<lane); #1;
        assert(word_hazard==(((entry==0 ? 8'h03 : 8'h0c)&query_mask)!=0)) else $fatal(1,"byte overlap");
      end
      query_address+=8; query_mask=8'hff; #1;
      assert(!word_hazard) else $fatal(1,"different word blocked");
      query_address+=64'h100000; #1;
      assert(!word_hazard) else $fatal(1,"same index different physical tag blocked");
    end
    repeat(10) tick();
    assert(urgent && drain_out.bits.data==64'h1122 && drain_out.bits.mark_dirty) else $fatal(1,"age or held payload");
    drain_in.ready=1;
    tick();
    assert(count==1 && drain_out.bits.address==64'h2000 && !urgent) else $fatal(1,"FIFO drain");
    enqueue_in='{valid:1,bits:'{address:64'h3000,way:0,data:64'h5566,mask:8'hff,mark_dirty:0}};
    tick(); enqueue_in='0; drain_in.ready=0;
    assert(count==1 && drain_out.bits.address==64'h3000 && drain_out.bits.data==64'h5566) else $fatal(1,"simultaneous enqueue/drain");
    drain_in.ready=1; tick();
    assert(empty && count==0 && !drain_out.valid) else $fatal(1,"final drain");
    $display("committed-store capacity, byte hazards, FIFO, and age passed");
    $finish;
  end
endmodule
