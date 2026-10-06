// SPDX-License-Identifier: Apache-2.0
module rv2wide_fetch_tb;
  typedef struct packed { logic [63:0] cause, value; } fault_t;
  typedef struct packed { logic valid; fault_t bits; } fault_flow_t;
  typedef struct packed { logic [63:0] pc; logic [31:0] instruction; fault_flow_t fault; } instruction_t;
  typedef struct packed { instruction_t fetched; logic [4:0] rd; logic write; logic [63:0] data; logic deferred; } retirement_t;
  typedef struct packed { logic valid; retirement_t bits; } retirement_flow_t;
  typedef struct packed { logic [1:0] disposition; logic [63:0] cause, value; } resolution_t;
  typedef struct packed { logic [63:0] pc, target; resolution_t resolution; } redirect_t;
  typedef struct packed { logic valid; redirect_t bits; } redirect_flow_t;
  typedef struct packed { logic valid; logic [63:0] bits; } start_t;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_t;
  typedef struct packed { logic valid; CHISnpFlit bits; } snp_t;
  typedef struct packed { ready_t requester; rsp_t response; } irsp_in_t;
  typedef struct packed { rsp_t requester; ready_t response; } irsp_out_t;
  typedef struct packed { ready_t request; dat_t response; } idat_in_t;
  typedef struct packed { dat_t request; ready_t response; } idat_out_t;
  typedef struct packed { ready_t req; irsp_in_t rsp; idat_in_t dat; } ichi_in_t;
  typedef struct packed { req_t req; irsp_out_t rsp; idat_out_t dat; } ichi_out_t;
  typedef struct packed { ready_t requests, requester_responses, request_data; rsp_t responses; dat_t response_data; snp_t snoops; } dchi_in_t;
  typedef struct packed { req_t requests; rsp_t requester_responses; dat_t request_data; ready_t responses, response_data, snoops; } dchi_out_t;
  logic clock=0, reset=1, halted;
  start_t start_in;
  retirement_flow_t retired[2], completed;
  redirect_flow_t redirect;
  logic [1:0] issued, retired_count;
  ichi_in_t instruction_chi_in;
  ichi_out_t instruction_chi_out;
  dchi_in_t data_chi_in;
  dchi_out_t data_chi_out;
  RV2Wide dut(.clock(clock), .reset(reset), .instruction_node_id(7'd2), .data_node_id(7'd3),
    .start_in(start_in), .halted(halted), .retired_0_out(retired[0]), .retired_1_out(retired[1]),
    .completed_out(completed), .redirect_out(redirect), .issued(issued), .retired_count(retired_count),
    .instruction_chi_in(instruction_chi_in), .instruction_chi_out(instruction_chi_out),
    .data_chi_in(data_chi_in), .data_chi_out(data_chi_out));
  always #5 clock=~clock;

  byte unsigned backing[4096], model_bytes[4096];
  logic [63:0] registers[32];
  retirement_t completions[$];
  int cycles=0, reference_pc=0, commits=0, dual_run=0, longest_dual=0;
  int ireads=0, dreads=0, acks=0, replay_count=0, branch_count=0, faults=0, phase=0;
  int wrong_path_reads=0, detached_refills=0, completions_seen=0;
  bit iactive=0, dactive=0, wactive=0;
  CHIReqFlit irequest, drequest, wrequest;
  int idue, ipacket, ddue, dpacket;
  int expected_fault_pc, expected_fault_cause;
  logic [63:0] expected_fault_value;

  function automatic logic [31:0] addi(int rd, rs, imm);
    return {12'(imm),5'(rs),3'b000,5'(rd),7'h13};
  endfunction
  function automatic logic [31:0] jal(int rd, imm);
    return {1'(imm>>20),10'(imm>>1),1'(imm>>11),8'(imm>>12),5'(rd),7'h6f};
  endfunction
  function automatic logic [31:0] bne(int rs1, rs2, imm);
    return {1'(imm>>12),6'(imm>>5),5'(rs2),5'(rs1),3'b001,4'(imm>>1),1'(imm>>11),7'h63};
  endfunction
  task automatic insn(int pc, logic [31:0] word);
    for (int b=0;b<4;b++) backing[pc+b]=word[b*8+:8];
  endtask
  function automatic logic [31:0] instruction_at(int pc);
    logic [31:0] word;
    for (int b=0;b<4;b++) word[b*8+:8]=backing[pc+b];
    return word;
  endfunction

  // Public CHI transactions alone drive the byte-addressed backing store.
  // Instruction and data identities have independent retained transactions.
  always_comb begin
    instruction_chi_in='0;
    instruction_chi_in.req.ready=!iactive && cycles%5!=0;
    instruction_chi_in.rsp.requester.ready=cycles%4!=0;
    if (iactive && cycles>=idue) begin
      instruction_chi_in.dat.response.valid=1;
      instruction_chi_in.dat.response.bits.opcode=4'h4;
      instruction_chi_in.dat.response.bits.src_id=7'd1;
      instruction_chi_in.dat.response.bits.tgt_id=7'd2;
      instruction_chi_in.dat.response.bits.home_nid_or_pbha_or_mismatched_mecid=7'd1;
      instruction_chi_in.dat.response.bits.txn_id=irequest.txn_id;
      instruction_chi_in.dat.response.bits.dbid_or_mecid=16'd5;
      instruction_chi_in.dat.response.bits.data_id=2'(ipacket^1);
      instruction_chi_in.dat.response.bits.byte_enable='1;
      instruction_chi_in.dat.response.bits.resp_err=(irequest.address==192 || irequest.address==512) ? 2'b10 : 0;
      for (int b=0;b<16;b++) instruction_chi_in.dat.response.bits.data[b*8+:8]=backing[int'(irequest.address)+16*(ipacket^1)+b];
    end
    data_chi_in='0;
    data_chi_in.requests.ready=!dactive && !wactive && cycles%5!=0;
    data_chi_in.requester_responses.ready=cycles%4!=0;
    data_chi_in.request_data.ready=cycles%3!=0;
    if (wactive) begin
      data_chi_in.responses.valid=1;
      data_chi_in.responses.bits.opcode=5'h05;
      data_chi_in.responses.bits.src_id=7'd1;
      data_chi_in.responses.bits.tgt_id=7'd3;
      data_chi_in.responses.bits.txn_id=wrequest.txn_id;
      data_chi_in.responses.bits.dbid_or_group_id=12'd9;
    end
    if (dactive && cycles>=ddue) begin
      data_chi_in.response_data.valid=1;
      data_chi_in.response_data.bits.opcode=4'h4;
      data_chi_in.response_data.bits.src_id=7'd1;
      data_chi_in.response_data.bits.tgt_id=7'd3;
      data_chi_in.response_data.bits.home_nid_or_pbha_or_mismatched_mecid=7'd1;
      data_chi_in.response_data.bits.txn_id=drequest.txn_id;
      data_chi_in.response_data.bits.dbid_or_mecid=16'd6;
      data_chi_in.response_data.bits.resp=drequest.opcode==7'h07 ? 3'd2 : 3'd1;
      data_chi_in.response_data.bits.data_id=2'(dpacket);
      data_chi_in.response_data.bits.byte_enable='1;
      for (int b=0;b<16;b++) data_chi_in.response_data.bits.data[b*8+:8]=backing[int'(drequest.address)+16*dpacket+b];
    end
  end

  task automatic retire(retirement_t got);
    logic [31:0] word;
    logic [63:0] value, address;
    logic signed [63:0] imm;
    retirement_t expected;
    bit write_rd;
    int rd, rs1, rs2;
    assert((phase==0 || phase==4) && got.fetched.pc==64'(reference_pc) && !got.fetched.fault.valid)
      else $fatal(1,"retired wrong path/fault pc=%h expected=%h phase=%0d",got.fetched.pc,reference_pc,phase);
    word=instruction_at(reference_pc);
    assert(got.fetched.instruction==word) else $fatal(1,"fetch payload at %h",got.fetched.pc);
    rd=int'(word[11:7]); rs1=int'(word[19:15]); rs2=int'(word[24:20]);
    value=0; write_rd=0; reference_pc+=4;
    case(word[6:0])
      7'h13: begin value=registers[rs1]+64'($signed(word[31:20])); write_rd=rd!=0; end
      7'h6f: begin
        value=64'(reference_pc); write_rd=rd!=0;
        imm=64'($signed({word[31],word[19:12],word[20],word[30:21],1'b0}));
        reference_pc=int'(got.fetched.pc+imm);
      end
      7'h63: begin
        imm=64'($signed({word[31],word[7],word[30:25],word[11:8],1'b0}));
        if(registers[rs1]!=registers[rs2]) reference_pc=int'(got.fetched.pc+imm);
      end
      7'h03: begin
        address=registers[rs1]+64'($signed(word[31:20])); write_rd=rd!=0;
        for(int b=0;b<8;b++) value[b*8+:8]=model_bytes[int'(address)+b];
      end
      7'h23: begin
        imm=64'($signed({word[31:25],word[11:7]})); address=registers[rs1]+imm;
        for(int b=0;b<8;b++) model_bytes[int'(address)+b]=registers[rs2][b*8+:8];
      end
      default: $fatal(1,"oracle instruction %h",word);
    endcase
    assert(got.write==write_rd) else $fatal(1,"write flag at %h",got.fetched.pc);
    if(write_rd) begin
      assert(got.rd==5'(rd)) else $fatal(1,"rd");
      if(!got.deferred) assert(got.data==value) else $fatal(1,"pc=%h value=%h expected=%h",got.fetched.pc,got.data,value);
      registers[rd]=value;
    end
    if(got.deferred) begin expected=got; expected.data=value; completions.push_back(expected); end
    commits++;
  endtask

  always @(posedge clock) if(!reset) begin
    cycles<=cycles+1;
    if(cycles>15000) $fatal(1,"timeout phase=%0d pc=%h",phase,reference_pc);
    if(retired[0].valid && retired[1].valid) begin dual_run++; if(dual_run>longest_dual) longest_dual=dual_run; end
    else dual_run=0;
    for(int lane=0;lane<2;lane++) if(retired[lane].valid) retire(retired[lane].bits);
    if(completed.valid) begin
      retirement_t expected;
      assert(completions.size()>0) else $fatal(1,"orphan completion");
      expected=completions.pop_front();
      assert(completed.bits.fetched==expected.fetched && completed.bits.write==expected.write && completed.bits.rd==expected.rd)
        else $fatal(1,"completion owner");
      if(expected.write) assert(completed.bits.data==expected.data) else $fatal(1,"completion data");
      completions_seen++;
    end
    if(redirect.valid) begin
      case(redirect.bits.resolution.disposition)
        0: begin branch_count++; if(iactive) detached_refills++; end
        2: replay_count++;
        1: begin
          assert(redirect.bits.pc==64'(expected_fault_pc) && redirect.bits.resolution.cause==64'(expected_fault_cause) && redirect.bits.resolution.value==expected_fault_value)
            else $fatal(1,"fault phase=%0d pc=%h cause=%h value=%h",phase,redirect.bits.pc,redirect.bits.resolution.cause,redirect.bits.resolution.value);
          assert(completions.size()==0) else $fatal(1,"fault before accepted work drains");
          if(phase==0) assert(reference_pc==4096 && registers[30]==77) else $fatal(1,"lost page-end instruction");
          if(phase==4) assert(reference_pc==652) else $fatal(1,"lost older load or retired after illegal instruction");
          faults++;
        end
        default: $fatal(1,"redirect disposition");
      endcase
    end
    if(instruction_chi_out.req.valid && instruction_chi_in.req.ready) begin
      assert(instruction_chi_out.req.bits.opcode==7'h03 && instruction_chi_out.req.bits.address<4096 && instruction_chi_out.req.bits.address[5:0]==0)
        else $fatal(1,"invalid instruction transaction");
      irequest<=instruction_chi_out.req.bits; iactive<=1; ipacket<=0; idue<=cycles+15; ireads++;
      if(instruction_chi_out.req.bits.address==192) wrong_path_reads++;
    end
    if(instruction_chi_in.dat.response.valid && instruction_chi_out.dat.response.ready) begin
      if(ipacket==3) iactive<=0;
      else begin ipacket<=ipacket+1; idue<=cycles+2; end
    end
    if(instruction_chi_out.rsp.requester.valid && instruction_chi_in.rsp.requester.ready) acks++;
    if(data_chi_out.requests.valid && data_chi_in.requests.ready) begin
      assert(data_chi_out.requests.bits.address<4096) else $fatal(1,"unmapped data transaction");
      case(data_chi_out.requests.bits.opcode)
        7'h02,7'h07: begin drequest<=data_chi_out.requests.bits; dactive<=1; dpacket<=0; ddue<=cycles+70; dreads++; end
        7'h1b: begin wrequest<=data_chi_out.requests.bits; wactive<=1; end
        default: $fatal(1,"data opcode");
      endcase
    end
    if(data_chi_in.response_data.valid && data_chi_out.response_data.ready) begin
      if(dpacket==3) dactive<=0;
      else begin dpacket<=dpacket+1; ddue<=cycles+2; end
    end
    if(data_chi_in.responses.valid && data_chi_out.responses.ready) wactive<=0;
    if(data_chi_out.request_data.valid && data_chi_in.request_data.ready)
      for(int b=0;b<16;b++) if(data_chi_out.request_data.bits.byte_enable[b])
        backing[int'(wrequest.address)+16*int'(data_chi_out.request_data.bits.data_id)+b]=data_chi_out.request_data.bits.data[b*8+:8];
  end

  task automatic launch(int address, cause, fault_pc, logic [63:0] fault_value='1);
    @(negedge clock);
    assert(halted) else $fatal(1,"start before halt");
    expected_fault_pc=fault_pc; expected_fault_cause=cause;
    expected_fault_value=fault_value=='1 ? 64'(fault_pc) : fault_value;
    start_in='{valid:1'b1,bits:64'(address)};
    @(negedge clock); start_in='0;
    wait(halted);
    repeat(6) @(negedge clock);
  endtask

  initial begin
    start_in='0;
    for(int p=0;p<4096;p+=4) insn(p,addi(0,0,0));
    for(int b=2048;b<4092;b++) backing[b]=8'(b);
    for(int b=0;b<4096;b++) model_bytes[b]=backing[b];
    for(int r=0;r<32;r++) registers[r]=0;
    insn(0,addi(31,0,3)); insn(4,addi(1,0,2047)); insn(8,addi(1,1,1)); insn(12,jal(0,52));
    for(int p=64;p<120;p+=4) insn(p,addi(2+(p-64)/4,0,p));
    insn(120,addi(31,31,-1)); insn(124,bne(31,0,-60));
    insn(128,{12'd0,5'd1,3'b011,5'd16,7'h03}); // delayed LD
    insn(132,addi(17,16,1)); // hold issue, filling reserved fetch capacity
    insn(136,addi(18,17,1)); insn(140,addi(19,18,1));
    insn(144,{7'd0,5'd19,5'd1,3'b011,5'd8,7'h23}); // SD
    insn(148,{12'd8,5'd1,3'b011,5'd20,7'h03});
    insn(188,jal(21,128)); // upper-only restart at 316; erroneous younger line 192 must be discarded
    insn(316,addi(22,21,1));
    insn(320,jal(0,4092-320));
    insn(4092,addi(30,0,77));
    // An older cold load must finish before the younger illegal instruction
    // reports its fault. Fetch cancellation happens before that delayed report.
    insn(640,addi(1,0,2047)); insn(644,addi(1,1,65));
    insn(648,{12'd0,5'd1,3'b011,5'd16,7'h03}); insn(652,32'hffffffff);
    repeat(4) @(negedge clock); reset=0;
    launch(0,1,4096);
    assert(longest_dual>=5 && dreads>0 && completions_seen>0 && branch_count>=4)
      else $fatal(1,"missing throughput/memory coverage dual=%0d dreads=%0d complete=%0d branches=%0d",longest_dual,dreads,completions_seen,branch_count);
    assert(wrong_path_reads>0 && detached_refills>0) else $fatal(1,"no wrong-path retained refill");
    phase=1; launch(512,1,512); // accepted CHI error, not illegal-instruction decoding
    phase=2; launch(514,0,514); // bad PC faults without issuing an aligned read
    phase=3; launch(4096,1,4096); // unmapped restart never reaches CHI
    phase=4; reference_pc=640; launch(640,2,652,64'hffffffff);
    assert(faults==5 && acks==ireads) else $fatal(1,"lost fault or retained acknowledgement faults=%0d ack=%0d read=%0d",faults,acks,ireads);
    $display("RV2Wide fetching core passed: %0d retirements, %0d-cycle dual run, %0d I refills, %0d D refills, %0d faults",commits,longest_dual,ireads,dreads,faults);
    $finish;
  end
endmodule
