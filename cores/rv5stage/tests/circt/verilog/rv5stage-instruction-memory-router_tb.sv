// Checks S1/S2 routing, explicit replay, exactly-once uncached work, and cancellation.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_instruction_memory_router_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; RV5StageFetchResult bits; } result_t;
  typedef struct packed {
    logic flush, invalidate_all, s1_kill;
    struct packed { logic valid; RV5StagePhysicalInstructionReq bits; } request;
  } core_in_t;
  typedef struct packed { result_t response; } pipeline_out_t;
  typedef struct packed {
    logic flush, invalidate_all, s1_kill;
    struct packed { logic valid; RV5StageInstructionReq bits; } request;
  } cache_out_t;
  typedef struct packed {
    ready_t request;
    struct packed { logic valid; RV5StageInstructionResp bits; } response;
  } uncached_in_t;
  typedef struct packed {
    logic flush, invalidate_all;
    struct packed { logic valid; RV5StagePhysicalInstructionReq bits; } request;
    ready_t response;
  } uncached_out_t;
  logic clock=0, reset=1;
  core_in_t core_in='0;
  pipeline_out_t core_out, cache_in='0;
  cache_out_t cache_out;
  uncached_in_t uncached_in='0;
  uncached_out_t uncached_out;
  bit cache_replay=0;
  int transactions=0;
  RV5StageInstructionMemoryRouter dut(.*);
  always #5 clock=~clock;
  always @(posedge clock) begin
    if(reset || cache_out.flush) cache_in.response <= '0;
    else begin
      cache_in.response.valid <= cache_out.request.valid;
      cache_in.response.bits <= '{response:'{word:cache_out.request.bits.address[31:0],page_fault:0,access_fault:0},replay:cache_replay};
    end
    if(!reset && uncached_out.request.valid && uncached_in.request.ready) transactions++;
  end
  task automatic tick;
    @(posedge clock); #1;
  endtask
  task automatic attempt(input logic [63:0] address, input bit cached, replay, input logic [31:0] word=0);
    core_in.request='{valid:1,bits:'{address:address,cacheable:cached,device:0}};
    tick();
    core_in.request.valid=0;
    assert(core_out.response.valid && core_out.response.bits.replay==replay)
      else $fatal(1,"incorrect S2 outcome at %h",address);
    if(!replay) assert(core_out.response.bits.response.word==word)
      else $fatal(1,"wrong word for S2 attempt");
    tick();
  endtask
  initial begin
    repeat(2) tick(); reset=0;
    uncached_in.request.ready=1;

    // Back-to-back cached words have no response ownership queue.
    core_in.request='{valid:1,bits:'{address:64'h1000,cacheable:1,device:0}};
    tick();
    assert(core_out.response.valid && !core_out.response.bits.replay && core_out.response.bits.response.word=='h1000);
    core_in.request.bits.address='h1004;
    tick();
    assert(core_out.response.valid && !core_out.response.bits.replay && core_out.response.bits.response.word=='h1004);
    core_in.request.valid=0; tick();
    cache_replay=1;
    attempt('h2000,1,1);
    cache_replay=0;
    attempt('h2000,1,0,'h2000);

    attempt('hc000,0,1);
    repeat(4) attempt('hc000,0,1);
    assert(transactions==1) else $fatal(1,"local replay duplicated an uncached read");
    uncached_in.response='{valid:1,bits:'{word:32'h12345678,page_fault:0,access_fault:0}};
    tick(); uncached_in.response.valid=0;
    attempt('hc000,0,0,32'h12345678);
    assert(transactions==1);

    // Killing S1 must not issue IO or suppress the preceding cached S2 word.
    core_in.request='{valid:1,bits:'{address:64'h1008,cacheable:1,device:0}};
    tick();
    core_in.request.bits='{address:64'hd000,cacheable:0,device:1};
    core_in.s1_kill=1;
    #1;
    assert(core_out.response.valid && core_out.response.bits.response.word=='h1008);
    tick();
    core_in.request.valid=0; core_in.s1_kill=0;
    assert(!core_out.response.valid && !uncached_out.request.valid);
    assert(transactions==1);

    // A stalled uncached request may be retried, but only acceptance owns work.
    uncached_in.request.ready=0;
    repeat(3) attempt('hd000,0,1);
    assert(transactions==1);
    uncached_in.request.ready=1;
    attempt('hd000,0,1);
    assert(transactions==2);
    core_in.flush=1; core_in.invalidate_all=1;
    #1;
    assert(cache_out.flush && cache_out.invalidate_all && uncached_out.flush && uncached_out.invalidate_all);
    assert(!core_out.response.valid && !uncached_out.request.valid && !uncached_out.response.ready);
    tick();
    core_in.flush=0; core_in.invalidate_all=0;
    attempt('h1010,1,0,'h1010);
    assert(transactions==2);
    $display("RV5Stage fixed-latency instruction routing and uncached replay passed");
    $finish;
  end
  initial begin #100000; $fatal(1,"instruction router timeout"); end
endmodule
