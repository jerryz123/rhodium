// Checks flow-through admission, head-owned certification, and page-window lifetime through replay.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_vector_admission_tb;
  logic clock=0, reset=1;
  logic [31:0] instruction=0;
  logic [63:0] vl=1, vtype=24, scalar=0;
  logic request_valid=0;
  logic retry_last=0;
  logic [63:0] retry_address=0;
  logic slow_access=0;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; } pulse_t;
  typedef struct packed { logic valid; logic bits; } check_response_t;
  typedef struct packed { logic valid; RV5StageVectorRange bits; } check_request_t;
  struct packed { ready_t request; check_response_t response; } precheck_in;
  struct packed { check_request_t request; pulse_t release_0; ready_t response; } precheck_out;
  struct packed { logic valid; RV5StagePipelineReq bits; } accesses_out;
  struct packed { logic valid; RV5StageDataReq bits; } memory_requests_out;
  struct packed { logic valid; RV5StageDataResp bits; } memory_responses_in;
  wire request_ready, active, sequencing, certification_pending, loads_pending, stores_pending, fp_pending;
  wire retired, outcome_valid, fp_offered;
  wire [63:0] outcome_pc;
  RV5StageVectorAdmission dut(.*);
  int cycle=0, launches=0, completions=0, outcomes=0, checks=0, releases=0, accesses=0, memory_requests=0;
  logic [63:0] addresses[256], data[256], last_outcome;
  RV5StageDataReq pending_memory[8];
  bit accepted;
  bit watch_window=0;
  int expected_window_accesses=0;

  function automatic logic [31:0] move_insn(input int rd, value);
    return 32'h5e003057 | (32'(rd)<<7) | (32'(value)<<15);
  endfunction
  function automatic logic [31:0] memory_insn(input int rd, input bit store);
    return (store ? 32'h02007027 : 32'h02007007) | (32'(rd)<<7);
  endfunction
  function automatic logic [31:0] load16_insn(input int rd);
    return 32'h02005007 | (32'(rd)<<7);
  endfunction
  task automatic tick;
    #2;
    accepted=request_valid && request_ready;
    if(!reset) begin
      if(accepted) launches++;
      if(retired) completions++;
      if(outcome_valid) begin
        outcomes++; last_outcome=outcome_pc;
        if(outcome_pc==64'h300 || outcome_pc==64'h400)
          assert(!certification_pending && !request_ready) else $fatal(1,"successful certification did not release scalar Decode before vector retirement");
      end
      if(precheck_out.request.valid && precheck_in.request.ready) checks++;
      if(precheck_out.release_0.valid) releases++;
      if(watch_window && precheck_out.release_0.valid)
        assert(accesses>=expected_window_accesses) else $fatal(1,"page window released before current memory macro's final authorized attempt: accesses=%0d expected=%0d",accesses,expected_window_accesses);
      if(accesses_out.valid) begin
        assert(accesses<256) else $fatal(1,"too many accesses");
        addresses[accesses]=accesses_out.bits.address;
        data[accesses]=accesses_out.bits.data;
        accesses++;
      end
      if(memory_requests_out.valid) begin
        assert(memory_requests<8) else $fatal(1,"too many delayed memory requests");
        pending_memory[memory_requests++]=memory_requests_out.bits;
      end
    end
    #3 clock=1; #1 clock=0; #4;
    cycle++;
    assert(cycle<4000) else $fatal(1,"admission timeout: accepted=%0d completed=%0d checks=%0d outcomes=%0d",launches,completions,checks,outcomes);
  endtask
  task automatic clear;
    reset=1; request_valid=0; retry_last=0; retry_address=0; slow_access=0; watch_window=0; precheck_in='0; memory_responses_in='0; tick(); reset=0;
    launches=0; completions=0; outcomes=0; checks=0; releases=0; accesses=0; memory_requests=0;
    #1;
    assert(!active && !loads_pending && !stores_pending && !fp_pending && !certification_pending)
      else $fatal(1,"reset retained queued status");
  endtask
  task automatic launch(input logic [31:0] insn, input logic [63:0] pc, length, configuration);
    instruction=insn; scalar=pc; vl=length; vtype=configuration; request_valid=1;
    do tick(); while(!accepted);
    request_valid=0;
  endtask
  task automatic drain;
    // The existing store-drain barrier clears after the last macro retires.
    do tick(); while(active || stores_pending);
    assert(completions==launches) else $fatal(1,"lost or duplicated queued instruction");
    assert(!loads_pending && !stores_pending && !fp_pending && !certification_pending)
      else $fatal(1,"queued status did not drain: cycle=%0d loads=%0b stores=%0b fp=%0b certification=%0b launches=%0d",cycle,loads_pending,stores_pending,fp_pending,certification_pending,launches);
  endtask
  task automatic certify(input logic [63:0] first_address, last_address, input bit safe);
    while(!precheck_out.request.valid) tick();
    repeat(3) begin
      #1;
      assert(precheck_out.request.valid && precheck_out.request.bits.first==first_address && precheck_out.request.bits.last==last_address)
        else $fatal(1,"precheck did not retain queue head");
      assert(certification_pending && !request_ready) else $fatal(1,"younger admission passed uncertified memory");
      tick();
    end
    precheck_in.request.ready=1; tick(); precheck_in.request.ready=0;
    repeat(3) begin
      assert(!precheck_out.request.valid && !request_ready) else $fatal(1,"duplicate precheck or early admission");
      tick();
    end
    precheck_in.response='{valid:1, bits:safe};
    #1; assert(precheck_out.response.ready) else $fatal(1,"head lost precheck ownership");
    tick(); precheck_in.response='0;
  endtask
  task automatic return_memory(input int index);
    memory_responses_in.valid=1;
    memory_responses_in.bits.access_fault=0;
    memory_responses_in.bits.data=pending_memory[index].address;
    memory_responses_in.bits.writeback=pending_memory[index].writeback;
    tick(); memory_responses_in='0;
  endtask
  initial begin
    precheck_in='0;
    clear();
    launch(move_insn(8,1),64'h1,1,24);
    #1;
    assert(sequencing) else $fatal(1,"idle descriptor did not enter sequencer on admission edge");
    drain();
    clear();
    // One descriptor per cycle, across both queue pointers and owner-ring wrap.
    for(int n=0;n<16;n++) begin
      instruction=move_insn(n+8,1); scalar=64'(n); vl=1; vtype=24; request_valid=1;
      tick();
      assert(accepted) else $fatal(1,"single-beat stream suffered admission bubble");
    end
    request_valid=0; drain();

    // A long current macro blocks a same-destination head. The second waiting
    // entry is FP: its status must be visible before it reaches the sequencer.
    clear();
    launch(move_insn(8,1),64'h10,16,27);
    launch(move_insn(8,2),64'h20,16,27);
    launch(32'h02001057 | (32'd8<<20) | (32'd10<<15) | (32'd24<<7),64'h30,1,24);
    instruction=move_insn(26,3); scalar=64'h40; request_valid=1;
    repeat(3) begin
      #1;
      assert(!request_ready && fp_pending && !fp_offered) else $fatal(1,"two-entry capacity or tail FP summary");
      tick(); assert(!accepted) else $fatal(1,"full queue accepted a third waiting descriptor");
    end
    clear();

    // A full compute queue replaces its departing head without an admission
    // bubble; the producer holds the rejected offer instead of replaying it.
    launch(move_insn(8,1),64'h10,16,27);
    launch(move_insn(8,2),64'h20,16,27);
    launch(move_insn(16,3),64'h30,16,27);
    instruction=move_insn(24,4); scalar=64'h40; vl=1; vtype=24; request_valid=1;
    repeat(3) begin tick(); assert(!accepted) else $fatal(1,"expected occupied waiting queue"); end
    do tick(); while(!accepted);
    request_valid=0; drain();

    // The tail store must be visible immediately, but must not start checking
    // until the preceding compute descriptor has left the queue.
    clear();
    launch(move_insn(8,1),64'h10,16,27);
    launch(move_insn(16,2),64'h20,16,27);
    launch(memory_insn(24,1),64'h300,2,24);
    #1;
    assert(stores_pending && !loads_pending && certification_pending && !precheck_out.request.valid)
      else $fatal(1,"tail memory summary or out-of-order precheck");
    // Live input changes must not alter the captured memory range or its PC.
    instruction=move_insn(4,7); scalar=64'hdead; vl=1; vtype=0;
    certify(64'h300,64'h30f,0);
    assert(certification_pending) else $fatal(1,"failed precheck released scalar Decode before final authorization");
    drain();
    assert(checks==1 && releases==1 && outcomes==1 && last_outcome==64'h300 && accesses==2 && addresses[0]==64'h300 && addresses[1]==64'h308)
      else $fatal(1,"failed certificate did not preserve ordered elementwise execution");

    // Successful certification permits younger compute admission while the
    // memory owner retains its certificate. Distinct queued operands survive.
    clear();
    launch(memory_insn(8,0),64'h400,16,27);
    certify(64'h400,64'h47f,1);
    while(certification_pending) tick();
    launch(move_insn(24,7),64'h50,1,24);
    launch(move_insn(25,9),64'h60,1,24);
    drain();
    assert(checks==1 && releases==1 && outcomes==1 && accesses==16 && last_outcome==64'h400)
      else $fatal(1,"certificate was overwritten by a younger descriptor");
    for(int n=0;n<16;n++) assert(addresses[n]==64'h400+64'(n)*8) else $fatal(1,"certified range changed");
    launch(memory_insn(24,1),64'h500,1,24); certify(64'h500,64'h507,1); drain();
    launch(memory_insn(25,1),64'h508,1,24); certify(64'h508,64'h50f,1); drain();
    assert(data[16]==7 && data[17]==9) else $fatal(1,"queued instruction operands were overwritten");

    // An older delayed load holds completion slots while a compute macro
    // hands off sequencing to a packed load. The new page window must remain
    // held through the packed load's final-beat retry.
    clear();
    slow_access=1;
    launch(memory_insn(8,0),64'h600,8,27);
    certify(64'h600,64'h63f,1);
    while(memory_requests<8) tick();
    while(releases<1) tick();
    slow_access=0;
    launch(move_insn(16,13),64'h680,64,11);
    watch_window=1; expected_window_accesses=25;
    retry_last=1; retry_address=64'h778;
    launch(load16_insn(24),64'h700,64,11);
    certify(64'h700,64'h77f,1);
    for(int n=0;n<8;n++) return_memory(n);
    drain();
    assert(checks==2 && releases==2 && accesses==25 && outcomes==2 && last_outcome==64'h700)
      else $fatal(1,"overlapped certified memory lost its page window or final replay");
    for(int n=8;n<25;n++) assert(addresses[n]>=64'h700 && addresses[n]<=64'h77f) else $fatal(1,"overlapped memory left certified range");

    clear();
    launch(memory_insn(8,0),64'h600,0,24); drain();
    assert(checks==0 && releases==0 && accesses==0 && outcomes==1 && last_outcome==64'h600)
      else $fatal(1,"empty memory did not certify once at dispatch");
    $display("Vector admission passed: queue ownership, certification, delayed responses, final replay, and descriptor snapshots");
    $finish;
  end
endmodule
