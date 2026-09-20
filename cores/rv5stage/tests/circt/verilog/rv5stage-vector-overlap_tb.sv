// Checks final-acceptance release, row chaining, cross-route ownership, WAW interlocks, and replay.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_vector_overlap_tb;
  logic clock=0, reset=1;
  logic [31:0] instruction=0;
  logic [63:0] vl=2, vtype=24, scalar=0, hit_data=0;
  logic request_valid=0, packed_memory=0, retry=0, slow=0, cancel=0;
  struct packed { logic valid; RV5StageVectorCompletion bits; } response_in;
  struct packed { logic ready; } fp_request_in;
  struct packed { logic valid; RV5StageFpExecutionRequest bits; } fp_request_out;
  struct packed { logic valid; RV5StageFpExecutionResult bits; } fp_result_in;
  struct packed { logic ready; } fp_result_out;
  struct packed { logic valid; RV5StageVectorToken bits; } attempt_out;
  wire request_ready, active, issued, issue_finished, retired;
  RV5StageVectorOverlap dut(.*);
  int cycle=0, done_count=0, retired_count=0, fp_count=0, memory_count=0, store_count=0;
  int fp_tags[8], memory_tags[8];
  logic [63:0] stores[8];
  bit launch_seen, retry_last=0, retried=0;

  function automatic logic [31:0] load_insn(input int rd);
    return 32'h02007007 | (32'(rd)<<7);
  endfunction
  function automatic logic [31:0] store_insn(input int rs);
    return 32'h02007027 | (32'(rs)<<7);
  endfunction
  task automatic tick;
    #1;
    hit_data=64'h1000+attempt_out.bits.address;
    retry=retry_last && !retried && attempt_out.valid && attempt_out.bits.last;
    #1;
    launch_seen=request_valid && request_ready;
    if (!reset) begin
      if (issue_finished) done_count++;
      if (retired) retired_count++;
      if (fp_request_out.valid && fp_request_in.ready) begin
        assert(fp_count<8) else $fatal(1,"too many FP requests");
        fp_tags[fp_count++]=int'(fp_request_out.bits.tag);
      end
      if (attempt_out.valid && !cancel) begin
        if (retry) retried=1;
        else if (attempt_out.bits.memory) begin
          if (slow) memory_tags[memory_count++]=int'(attempt_out.bits.completion_tag);
          if (attempt_out.bits.context_0>=64'h300) stores[store_count++]=attempt_out.bits.store_data;
        end
      end
    end
    #3 clock=1; #1 clock=0; #4;
    cycle++;
    assert(cycle<3000) else $fatal(1,"overlap test timed out");
  endtask
  task automatic launch(input logic [31:0] insn, input logic [63:0] base, input bit packed_mode);
    instruction=insn; scalar=base; packed_memory=packed_mode; request_valid=1;
    do tick(); while (!launch_seen);
    request_valid=0;
  endtask
  task automatic drain;
    do tick(); while(active);
  endtask
  task automatic return_fp(input int index, input logic [63:0] value);
    fp_result_in='0; fp_result_in.valid=1;
    fp_result_in.bits.tag=3'(fp_tags[index]);
    fp_result_in.bits.fp_value=value;
    fp_result_in.bits.exception_flags_valid=1;
    #1; assert(fp_result_out.ready) else $fatal(1,"lost FP result capacity");
    tick(); fp_result_in='0;
  endtask
  task automatic return_memory(input int index, input logic [63:0] value);
    response_in.valid=1; response_in.bits.tag=3'(memory_tags[index]); response_in.bits.data=value;
    tick(); response_in='0;
  endtask
  initial begin
    response_in='0; fp_result_in='0; fp_request_in.ready=1;
    tick(); reset=0;
    launch(load_insn(8),64'h100,0); drain();
    launch(load_insn(10),64'h180,1); drain();

    // The older FP descriptor releases only after its final local acceptance,
    // while neither service result has returned. A dependent packed store is
    // admitted immediately and chains on each completed 64-bit register row.
    launch(32'h02001057 | (32'd8<<20) | (32'd10<<15) | (32'd12<<7),64'h200,0);
    instruction=store_insn(12); scalar=64'h300; packed_memory=1; request_valid=1;
    do begin
      tick();
      assert(!launch_seen || done_count==3) else $fatal(1,"unroller replaced a replayable FP instruction");
    end while(!launch_seen);
    request_valid=0;
    assert(active) else $fatal(1,"FP tail lost ownership");
    repeat(8) tick();
    assert(fp_count==2) else $fatal(1,"accepted FP operands were not retained");
    assert(store_count==0) else $fatal(1,"store read unfinished FP row");
    return_fp(0,64'h1111222233334444);
    while(store_count<1) tick();
    assert(active && stores[0]==64'h1111222233334444) else $fatal(1,"first row did not chain");
    repeat(5) tick();
    assert(store_count==1) else $fatal(1,"second row bypassed dependency");
    retry_last=1;
    return_fp(1,64'h5555666677778888);
    drain();
    assert(retried && store_count==2 && stores[1]==64'h5555666677778888) else $fatal(1,"final retry lost/duplicated store");
    retry_last=0;

    // A packed load tail keeps its alignment/route metadata when the unroller
    // switches to an ordinary store. Return younger data first; VRF drain and
    // store operands must still be ordered and associated with the old owner.
    memory_count=0; store_count=0; slow=1;
    launch(load_insn(14),64'h100,1);
    while(memory_count<2) tick();
    slow=0;
    launch(store_insn(14),64'h380,0);
    repeat(5) tick();
    assert(store_count==0) else $fatal(1,"ordinary store read pending packed load");
    return_memory(1,64'hfedcba9876543210);
    repeat(5) tick();
    assert(store_count==0) else $fatal(1,"younger response bypassed ordered drain");
    return_memory(0,64'h0123456789abcdef);
    drain();
    assert(store_count==2 && stores[0]==64'h0123456789abcdef && stores[1]==64'hfedcba9876543210) else $fatal(1,"cross-route response ownership");

    // WAW cannot replace an older outstanding destination, even with a free
    // macro context. This also exercises allocator wrap without a per-launch reset.
    memory_count=0; slow=1;
    launch(load_insn(16),64'h100,0);
    while(memory_count<2) tick();
    instruction=load_insn(16); scalar=64'h180; request_valid=1; packed_memory=1;
    repeat(6) begin tick(); assert(!launch_seen) else $fatal(1,"WAW admitted too early"); end
    request_valid=0; slow=0;
    return_memory(1,64'hbbbb); return_memory(0,64'haaaa);
    launch(load_insn(16),64'h180,1); drain();
    assert(!active && retired_count==8) else $fatal(1,"macro context leaked: %0d retirements",retired_count);

    // Cancellation preserves an accepted prefix even when it ends in a
    // partial VRF row rather than the descriptor's original final word.
    vl=16; vtype=0; memory_count=0; slow=1;
    launch(32'h02000007 | (32'd18<<7),64'h103,1);
    while(memory_count<1) tick();
    cancel=1; tick(); cancel=0; slow=0;
    return_memory(0,64'h0706050403020100); drain();
    assert(retired_count==8) else $fatal(1,"canceled macro retired");
    vl=1; vtype=24; store_count=0;
    launch(store_insn(18),64'h400,0); drain();
    assert(store_count==1 && stores[0][39:0]==40'h0706050403 && retired_count==9) else $fatal(1,"canceled accepted carry was lost");
    $display("Vector overlap passed: final acceptance, row chaining, replay, cross-route returns, WAW, slot wrap, and canceled carry");
    $finish;
  end
endmodule
