// Checks bubble-free aligned and straddling instruction streams through the warm MMU/router/L1I path.
module rv5stage_fetch_throughput_tb;
  typedef struct packed {logic ready;} ready_t;
  typedef struct packed {logic valid; CHIReqFlit bits;} req_t;
  typedef struct packed {logic valid; CHIRspFlit bits;} rsp_t;
  typedef struct packed {logic valid; CHIDatFlit bits;} dat_t;
  typedef struct packed {logic valid; CHISnpFlit bits;} snp_t;
  typedef struct packed {ready_t requests, requester_responses, request_data; rsp_t responses; dat_t response_data; snp_t snoops;} chi_in_t;
  typedef struct packed {req_t requests; rsp_t requester_responses; dat_t request_data; ready_t responses, response_data, snoops;} chi_out_t;
  logic clock=0, reset=1, active=0, restart=0, sink_ready=1;
  logic [63:0] start_pc=0, pc;
  logic valid, fault;
  logic [31:0] instruction;
  chi_in_t chi_in;
  chi_out_t chi_out;
  RV5StageFetchThroughput dut(.*);
  always #5 clock=~clock;
  integer cycle=0, offset=0, requests=0, beat=0;
  logic pending=0;
  logic [63:0] line_address;
  logic [11:0] transaction;

  function automatic logic [31:0] expected_instruction(input int index);
    return index[0] ? 32'h01f363b3 : 32'h0152f333;
  endfunction
  function automatic logic [7:0] byte_at(input logic [63:0] address);
    int relative_address;
    logic [31:0] word;
    relative_address=int'(address-64'h1000)-offset;
    if(relative_address<0) return address[0] ? 8'h00 : 8'h01;
    word=expected_instruction(relative_address/4);
    return word[8*(relative_address%4)+:8];
  endfunction
  always_comb begin
    chi_in='0;
    chi_in.requests.ready=1;
    chi_in.requester_responses.ready=1;
    chi_in.request_data.ready=1;
    chi_in.response_data.valid=pending;
    chi_in.response_data.bits.opcode=4'h4;
    chi_in.response_data.bits.resp=3'b001;
    chi_in.response_data.bits.byte_enable=16'hffff;
    chi_in.response_data.bits.data_id=2'(beat);
    chi_in.response_data.bits.home_nid_or_pbha_or_mismatched_mecid=7'd1;
    chi_in.response_data.bits.dbid_or_mecid=16'h55;
    chi_in.response_data.bits.txn_id=transaction;
    chi_in.response_data.bits.src_id=7'd1;
    chi_in.response_data.bits.tgt_id=7'd2;
    for(int b=0;b<16;b++) chi_in.response_data.bits.data[b*8+:8]=byte_at(line_address+64'(16*beat+b));
  end
  always @(posedge clock) begin
    cycle<=cycle+1;
    if(reset) begin pending<=0; beat<=0; requests<=0; end
    else begin
      if(chi_out.requests.valid && chi_in.requests.ready) begin
        assert(!pending && chi_out.requests.bits.opcode==7'h02) else $fatal(1,"unexpected refill request");
        line_address<=64'(chi_out.requests.bits.address);
        transaction<=chi_out.requests.bits.txn_id;
        pending<=1; beat<=0; requests<=requests+1;
      end
      if(pending && chi_out.response_data.ready) begin
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
    int sampled;
    seen=0; elapsed=0; first_cycle=0; last_cycle=0; bubbles=0;
    sampled=0;
    restart=1; active=1; tick(); restart=0;
    refills_before=requests;
    while(seen<256 && elapsed<20000) begin
      @(negedge clock);
      if(valid) begin
        assert(pc==start_pc+64'(4*seen) && instruction==expected_instruction(seen) && !fault)
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
      if(valid) seen++;
      tick(); elapsed++;
    end
    assert(seen==256) else $fatal(1,"stream timed out");
    active=0;
    repeat(100) tick();
  endtask
  initial begin
    for(int mode=0;mode<2;mode++) begin
      offset=mode*2; start_pc=64'h1000+64'(offset);
      reset=1; active=0; restart=0; repeat(4) tick(); reset=0;
      run_stream(0);
      run_stream(1);
    end
    $display("RV5Stage integrated aligned and straddling fetch throughput passed");
    $finish;
  end
endmodule
