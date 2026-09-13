// Checks cold-refill recovery and bubble-free aligned, straddling, and compressed fetch streams.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_fetch_throughput_tb;
  typedef struct packed {logic ready;} ready_t;
  typedef struct packed {logic valid; CHIReqFlit bits;} req_t;
  typedef struct packed {logic valid; CHIRspFlit bits;} rsp_t;
  typedef struct packed {logic valid; CHIDatFlit bits;} dat_t;
  typedef struct packed { ready_t requester; rsp_t response; } rsp_in_t;
  typedef struct packed { rsp_t requester; ready_t response; } rsp_out_t;
  typedef struct packed { ready_t request; dat_t response; } dat_in_t;
  typedef struct packed { dat_t request; ready_t response; } dat_out_t;
  typedef struct packed { ready_t req; rsp_in_t rsp; dat_in_t dat; } chi_in_t;
  typedef struct packed { req_t req; rsp_out_t rsp; dat_out_t dat; } chi_out_t;
  logic clock=0, reset=1, active=0, restart=0, sink_ready=1;
  logic [63:0] start_pc=0, pc;
  logic valid, fault;
  logic [31:0] instruction;
  chi_in_t chi_in;
  chi_out_t chi_out;
  RV5StageFetchThroughput dut(.*);
  always #5 clock=~clock;
  integer cycle=0, offset=0, requests=0, beat=0;
  bit compressed_mode=0, stall_mode=0;
  int line_requests[32];
  logic pending=0;
  logic [63:0] line_address;
  logic [11:0] transaction;

  function automatic logic [31:0] expected_instruction(input int index);
    if(compressed_mode) return 32'h13 | (32'((index%31)+1)<<7);
    return index[0] ? 32'h01f363b3 : 32'h0152f333;
  endfunction
  function automatic logic [7:0] byte_at(input logic [63:0] address);
    int relative_address;
    logic [31:0] word;
    relative_address=int'(address-64'h1000)-offset;
    if(relative_address<0) return address[0] ? 8'h00 : 8'h01;
    if(compressed_mode) begin
      word=32'h4001 | (32'(((relative_address/2)%31)+1)<<7);
      return word[8*(relative_address%2)+:8];
    end
    word=expected_instruction(relative_address/4);
    return word[8*(relative_address%4)+:8];
  endfunction
  always_comb begin
    chi_in='0;
    chi_in.req.ready=1;
    chi_in.rsp.requester.ready=1;
    chi_in.dat.request.ready=1;
    chi_in.dat.response.valid=pending && (!stall_mode || cycle%7>=3);
    chi_in.dat.response.bits.opcode=4'h4;
    chi_in.dat.response.bits.resp=3'b000;
    chi_in.dat.response.bits.byte_enable=16'hffff;
    chi_in.dat.response.bits.data_id=2'(beat);
    chi_in.dat.response.bits.home_nid_or_pbha_or_mismatched_mecid=7'd1;
    chi_in.dat.response.bits.dbid_or_mecid=16'h55;
    chi_in.dat.response.bits.txn_id=transaction;
    chi_in.dat.response.bits.src_id=7'd1;
    chi_in.dat.response.bits.tgt_id=7'd2;
    for(int b=0;b<16;b++) chi_in.dat.response.bits.data[b*8+:8]=byte_at(line_address+64'(16*beat+b));
  end
  always @(posedge clock) begin
    cycle<=cycle+1;
    if(reset) begin pending<=0; beat<=0; requests<=0; foreach(line_requests[i]) line_requests[i]<=0; end
    else begin
      if(chi_out.req.valid && chi_in.req.ready) begin
        assert(!pending && chi_out.req.bits.opcode==7'h03) else $fatal(1,"unexpected refill request");
        assert(chi_out.req.bits.address>=44'h1000 && chi_out.req.bits.address<44'h1800)
          else $fatal(1,"fetch escaped the bounded instruction image");
        assert(line_requests[5'((chi_out.req.bits.address-44'h1000)/64)]==0)
          else $fatal(1,"frontend replay duplicated a line refill");
        line_requests[5'((chi_out.req.bits.address-44'h1000)/64)]<=1;
        line_address<=64'(chi_out.req.bits.address);
        transaction<=chi_out.req.bits.txn_id;
        pending<=1; beat<=0; requests<=requests+1;
      end
      if(chi_in.dat.response.valid && chi_out.dat.response.ready) begin
        if(beat==3) pending<=0;
        else beat<=beat+1;
      end
    end
  end
  task automatic tick;
    @(posedge clock); #1;
  endtask
  task automatic run_stream(input bit measure);
    int seen, elapsed, first_cycle, last_cycle, bubbles, refills_before;
    int sampled, previous_delivery, stride, line_instructions;
    bit held;
    logic [63:0] held_pc;
    logic [31:0] held_instruction;
    held=0;
    previous_delivery=-1; stride=compressed_mode?2:4; line_instructions=64/stride;
    seen=0; elapsed=0; first_cycle=0; last_cycle=0; bubbles=0;
    sampled=0;
    restart=1; active=1; tick(); restart=0;
    refills_before=requests;
    while(seen<256 && elapsed<20000) begin
      @(negedge clock);
      sink_ready=!stall_mode || cycle%11>=4;
      #1;
      if(held) assert(valid && pc==held_pc && instruction==held_instruction)
        else $fatal(1,"completed word changed under Decode backpressure");
      held=valid && !sink_ready; held_pc=pc; held_instruction=instruction;
      if(valid) begin
        assert(pc==start_pc+64'(stride*seen) && instruction==expected_instruction(seen) && !fault)
          else $fatal(1,"bad instruction index=%0d pc=%h instruction=%h fault=%b",seen,pc,instruction,fault);
      end
      if(measure && seen>=32 && seen<224) begin
        if(sampled==0) begin first_cycle=cycle; refills_before=requests; end
        sampled++;
        if(valid) begin
          if(last_cycle!=0) bubbles+=cycle-last_cycle-1;
          last_cycle=cycle;
        end
        if(valid && seen==223) begin
          $display("fetch offset=%0d instructions=192 cycles=%0d bubbles=%0d refills=%0d",offset,last_cycle-first_cycle+1,bubbles,requests-refills_before);
          assert(requests==refills_before) else $fatal(1,"measurement included cold misses");
          assert(bubbles==0 && last_cycle-first_cycle+1==192) else $fatal(1,"warm fetch inserted a bubble at alignment %0d",offset);
        end
      end
      if(valid && sink_ready) begin
        // Do not restart after warming: measure the interior of every cold
        // line, leaving a small explicit refill-to-delivery recovery allowance.
        if(!measure && !stall_mode && seen%line_instructions>=4 && seen%line_instructions<line_instructions-2)
          assert(cycle==previous_delivery+1)
            else $fatal(1,"cold-refill bubble at instruction %0d compressed=%b offset=%0d gap=%0d",seen,compressed_mode,offset,cycle-previous_delivery);
        previous_delivery=cycle;
        seen++;
      end
      tick(); elapsed++;
    end
    assert(seen==256) else $fatal(1,"stream timed out");
    active=0;
    repeat(100) tick();
  endtask
  initial begin
    for(int mode=0;mode<4;mode++) begin
      compressed_mode=mode==2;
      stall_mode=mode==3;
      sink_ready=1;
      offset=(mode==1 || mode==3)?2:0; start_pc=64'h1000+64'(offset);
      reset=1; active=0; restart=0; repeat(4) tick(); reset=0;
      run_stream(0);
      if(!stall_mode) run_stream(1);
    end
    $display("RV5Stage integrated cold and warm fetch throughput passed");
    $finish;
  end
endmodule
