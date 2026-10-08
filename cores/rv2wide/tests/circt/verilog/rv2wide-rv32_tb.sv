// Exercises RV32 scalar execution, Bare PMAs, split accesses, and precise traps through the production fetching top.
// SPDX-License-Identifier: Apache-2.0
module rv2wide_rv32_tb;
  typedef struct packed { logic [31:0] cause, value; logic [65:0] guest; } fault_t;
  typedef struct packed { logic valid; fault_t bits; } fault_flow_t;
  typedef struct packed { logic valid; logic [31:0] pc, target; logic compressed; logic [1:0] ras_action; } prediction_t;
  typedef struct packed { logic valid; logic [11:0] index; logic [9:0] history; logic taken; } direction_t;
  typedef struct packed { logic [31:0] pc; logic [31:0] instruction, raw_instruction; logic [31:0] sequential_pc; logic compressed_illegal; fault_flow_t fault; prediction_t prediction; logic [1:0] speculated_ras_action; direction_t direction; } instruction_t;
  typedef struct packed { instruction_t fetched; logic [4:0] rd; logic write; logic [31:0] data; logic deferred; } retirement_t;
  typedef struct packed { logic valid; retirement_t bits; } retirement_flow_t;
  typedef struct packed { logic [1:0] disposition; logic [31:0] cause, value; logic [65:0] guest; } resolution_t;
  typedef struct packed { logic [31:0] pc, target; resolution_t resolution; } redirect_t;
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
  logic clock=0, reset=1, halted, sleeping;
  start_t start_in;
  retirement_flow_t retired[2], completed;
  redirect_flow_t redirect;
  logic [1:0] issued, retired_count;
  ichi_in_t instruction_chi_in;
  ichi_out_t instruction_chi_out;
  ichi_in_t uncached_chi_in;
  ichi_out_t uncached_chi_out;
  dchi_in_t data_chi_in;
  dchi_out_t data_chi_out;
  RV2Wide dut(.clock(clock), .reset(reset), .instruction_node_id(7'd2), .data_node_id(7'd3), .uncached_node_id(7'd4),
    .interrupts('0), .hart_id(64'd0), .time_counter(64'd0), .sleeping(sleeping),
    .start_in(start_in), .halted(halted), .retired_0_out(retired[0]), .retired_1_out(retired[1]),
    .completed_out(completed), .redirect_out(redirect), .issued(issued), .retired_count(retired_count),
    .instruction_chi_in(instruction_chi_in), .instruction_chi_out(instruction_chi_out),
    .data_chi_in(data_chi_in), .data_chi_out(data_chi_out), .uncached_chi_in(uncached_chi_in), .uncached_chi_out(uncached_chi_out));
  always #5 clock=~clock;


  byte unsigned backing[131072];
  logic [31:0] expected[131072];
  bit expected_write[131072];
  bit seen[131072];
  int pc='h400, cycles=0, commits=0, dual=0, faults=0, completed_count=0;
  int ireads=0, dreads=0, ureads=0, uwrites=0;
  logic [31:0] trap_pc, trap_value;
  bit iactive=0, dactive=0, wactive=0;
  CHIReqFlit irequest, drequest, wrequest, urequest;
  int idue, ipacket, ddue, dpacket, ustate=0, udue=0;

  function automatic logic [31:0] ri(int op, rd, rs, imm);
    return {12'(imm),5'(rs),3'(op),5'(rd),7'h13};
  endfunction
  function automatic logic [31:0] rr(int func, op, rd, rs1, rs2);
    return {7'(func),5'(rs2),5'(rs1),3'(op),5'(rd),7'h33};
  endfunction
  function automatic logic [31:0] csr(int op, address, rd, rs);
    return {12'(address),5'(rs),3'(op),5'(rd),7'h73};
  endfunction
  function automatic logic [31:0] ld(int op, rd, base, offset);
    return {12'(offset),5'(base),3'(op),5'(rd),7'h03};
  endfunction
  function automatic logic [31:0] st(int op, rs, base, offset);
    return {7'(offset>>5),5'(rs),5'(base),3'(op),5'(offset),7'h23};
  endfunction
  task automatic put(int address, logic [31:0] word);
    for(int b=0;b<4;b++) backing[address+b]=word[b*8+:8];
  endtask
  task automatic emit(logic [31:0] word, bit writes=0, logic [31:0] value=0);
    put(pc,word); expected_write[pc]=writes; expected[pc]=value; pc+=4;
  endtask
  task automatic check_result(retirement_t result, bit completion);
    int address=int'(result.fetched.pc);
    if(address=='h800 || address=='h804 || address=='h808) begin
      assert(result.write && result.data==(address=='h800 ? trap_pc : address=='h804 ? trap_value : trap_pc+4))
        else $fatal(1,"RV32 trap CSR value at %h: %h",address,result.data);
      return;
    end
    if(result.write && (completion || !result.deferred)) begin
      assert(expected_write[address] && result.data==expected[address])
        else $fatal(1,"RV32 result pc=%h rd=%0d got=%h expected=%h",address,result.rd,result.data,expected[address]);
      assert(!seen[address]) else $fatal(1,"duplicate result at %h",address);
      seen[address]=1;
    end
  endtask
  always_comb begin
    uncached_chi_in='0;
    uncached_chi_in.req.ready=ustate==0 && cycles%7!=0;
    uncached_chi_in.rsp.requester.ready=1;
    uncached_chi_in.dat.request.ready=ustate==3 && cycles%3!=0;
    if(ustate==1 && cycles>=udue) begin
      uncached_chi_in.dat.response.valid=1;
      uncached_chi_in.dat.response.bits.opcode=4'h4;
      uncached_chi_in.dat.response.bits.src_id=urequest.tgt_id;
      uncached_chi_in.dat.response.bits.home_nid_or_pbha_or_mismatched_mecid=urequest.tgt_id;
      uncached_chi_in.dat.response.bits.tgt_id=7'd4;
      uncached_chi_in.dat.response.bits.txn_id=urequest.txn_id;
      uncached_chi_in.dat.response.bits.byte_enable='1;
      for(int b=0;b<16;b++) uncached_chi_in.dat.response.bits.data[b*8+:8]=backing[(int'(urequest.address)&~15)+b];
    end
    if((ustate==2 || ustate==4) && cycles>=udue) begin
      uncached_chi_in.rsp.response.valid=1;
      uncached_chi_in.rsp.response.bits.opcode=ustate==2 ? 5'h06 : 5'h04;
      uncached_chi_in.rsp.response.bits.src_id=urequest.tgt_id;
      uncached_chi_in.rsp.response.bits.tgt_id=7'd4;
      uncached_chi_in.rsp.response.bits.txn_id=urequest.txn_id;
      uncached_chi_in.rsp.response.bits.dbid_or_group_id=12'd11;
    end
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
      instruction_chi_in.dat.response.bits.resp_err=0;
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

  always @(posedge clock) if(!reset) begin
    cycles<=cycles+1;
    assert(cycles<30000) else $fatal(1,"RV32 timed out");
    if(retired_count==2) dual++;
    for(int lane=0;lane<2;lane++) if(retired[lane].valid) begin
      commits++;
      check_result(retired[lane].bits,0);
    end
    if(completed.valid) begin check_result(completed.bits,1); completed_count++; end
    if(redirect.valid && redirect.bits.resolution.disposition==1) begin
      assert(redirect.bits.resolution.cause==2 || redirect.bits.resolution.cause==5)
        else $fatal(1,"unexpected RV32 trap %h at %h",redirect.bits.resolution.cause,redirect.bits.pc);
      faults++;
      trap_pc=redirect.bits.pc;
      if(redirect.bits.resolution.cause==2)
        for(int b=0;b<4;b++) trap_value[b*8+:8]=backing[int'(trap_pc)+b];
      else trap_value=redirect.bits.pc==32'(split_fault_pc) ? 32'h1000 : 32'hfffff800;
      assert(redirect.bits.resolution.value==trap_value) else $fatal(1,"fault address/encoding");
    end
    if(instruction_chi_out.req.valid && instruction_chi_in.req.ready) begin
      assert(instruction_chi_out.req.bits.address<4096 && instruction_chi_out.req.bits.opcode==7'h03) else $fatal(1,"bad fetch / unexpected page walk");
      irequest<=instruction_chi_out.req.bits; iactive<=1; ipacket<=0; idue<=cycles+12; ireads++;
    end
    if(instruction_chi_in.dat.response.valid && instruction_chi_out.dat.response.ready) begin
      if(ipacket==3) iactive<=0;
      else begin ipacket<=ipacket+1; idue<=cycles+2; end
    end
    if(data_chi_out.requests.valid && data_chi_in.requests.ready) begin
      assert(data_chi_out.requests.bits.address<4096) else $fatal(1,"bad data / unexpected page walk");
      case(data_chi_out.requests.bits.opcode)
        7'h02,7'h07: begin drequest<=data_chi_out.requests.bits; dactive<=1; dpacket<=0; ddue<=cycles+30; dreads++; end
        7'h1b: begin wrequest<=data_chi_out.requests.bits; wactive<=1; end
        default: $fatal(1,"bad data opcode");
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
    if(uncached_chi_out.req.valid && uncached_chi_in.req.ready) begin
      assert(!dactive && !wactive) else $fatal(1,"IO failed to drain");
      assert(uncached_chi_out.req.bits.address>='h2000 && uncached_chi_out.req.bits.address<'h2100 && uncached_chi_out.req.bits.size_or_num_req<=2) else $fatal(1,"RV32 IO address or width");
      urequest<=uncached_chi_out.req.bits;
      if(uncached_chi_out.req.bits.opcode==7'h04) begin ustate<=1; ureads++; end
      else if(uncached_chi_out.req.bits.opcode==7'h1c) begin ustate<=2; uwrites++; end
      else $fatal(1,"bad IO opcode");
      udue<=cycles+20;
    end
    if(uncached_chi_in.dat.response.valid && uncached_chi_out.dat.response.ready) ustate<=0;
    if(uncached_chi_in.rsp.response.valid && uncached_chi_out.rsp.response.ready) ustate<=ustate==2 ? 3 : 0;
    if(uncached_chi_out.dat.request.valid && uncached_chi_in.dat.request.ready) begin
      logic [15:0] mask;
      mask=16'((1<<(1<<urequest.size_or_num_req))-1)<<urequest.address[3:0];
      assert(uncached_chi_out.dat.request.bits.byte_enable==mask) else $fatal(1,"IO byte lanes");
      for(int b=0;b<16;b++) if(mask[b]) backing[(int'(urequest.address)&~15)+b]=uncached_chi_out.dat.request.bits.data[b*8+:8];
      ustate<=4; udue<=cycles+20;
    end
  end

  int split_fault_pc;
  initial begin
    start_in='0;
    foreach(backing[i]) begin backing[i]=0; expected[i]=0; expected_write[i]=0; seen[i]=0; end
    emit(32'h00001e37,1,32'h1000); // x28 = handler base - 0x800
    emit(ri(0,28,28,-2048),1,32'h800);
    emit(csr(1,'h305,0,28));
    emit(ri(0,2,0,-1),1,'1);
    emit(ri(0,3,2,1),1,0); // XLEN wrap
    emit(ri(1,4,2,31),1,32'h80000000);
    emit(ri(5,5,2,31),1,1);
    emit(ri(5,6,4,'h41f),1,'1);
    emit(rr(0,1,7,2,3),1,'1);
    emit(ri(0,8,0,3),1,3);
    emit(rr(1,0,9,2,8),1,32'hfffffffd);
    emit(rr(1,1,10,2,8),1,'1);
    emit(rr(1,2,11,2,8),1,'1);
    emit(rr(1,3,12,2,8),1,2);
    emit(rr(1,4,13,4,2),1,32'h80000000); // signed divide overflow
    emit(rr(1,5,14,2,8),1,32'h55555555);
    emit(rr(1,6,15,4,2),1,0);
    emit(rr(1,7,16,2,8),1,0);
    emit(rr(1,4,17,8,0),1,'1);
    emit(rr(1,6,18,8,0),1,3);
    emit(ri(5,19,8,'h698),1,32'h03000000); // RV32 REV8
    emit(ri(5,20,8,'h601),1,32'h80000001); // RV32 RORI
    emit(rr('h04,4,21,2,0),1,32'hffff); // RV32 ZEXT.H
    emit(ri(0,22,0,32),1,32);
    emit(rr(0,1,23,8,22),1,3); // register shift amount masked to five bits
    emit(ri(0,24,0,'hc00),1,32'hfffffc00);
    emit(32'h00001c37,1,32'h1000);
    emit(ri(0,24,24,-1024),1,32'hc00);
    for(int lane=0;lane<4;lane++) begin
      emit(st(0,8,24,lane));
      emit(ld(4,25,24,lane),1,3);
    end
    emit(st(2,4,24,1)); // cross-word misaligned store and load
    emit(ld(2,25,24,1),1,32'h80000000);
    emit(st(2,8,24,60));
    emit(ld(2,25,24,60),1,3);
    emit({5'd2,2'b11,5'd0,5'd24,3'd2,5'd25,7'h2f},1,32'h00000003); // LR.W
    emit({5'd3,2'b11,5'd8,5'd24,3'd2,5'd26,7'h2f},1,0); // SC.W
    emit({5'd0,2'b11,5'd8,5'd24,3'd2,5'd25,7'h2f},1,3); // AMOADD.W
    emit(ld(2,25,24,0),1,6);
    emit(32'h00002d37,1,32'h2000);
    emit(st(2,4,26,4));
    emit(ld(2,25,26,4),1,32'h80000000);
    emit(st(0,8,26,7));
    emit(ld(4,25,26,7),1,3);
    emit(csr(1,'h180,0,2)); // attempted Sv32 satp is WARL zero in Bare
    emit(csr(2,'h180,25,0),1,0);
    emit(csr(1,'h340,0,4));
    emit(csr(2,'h340,25,0),1,32'h80000000);
    emit(csr(2,'h301,25,0),1,32'h40141107); // RV32 MISA: IMACB + S/U
    emit(ri(0,27,0,5),1,5);
    emit(csr(1,'h320,0,27)); // Freeze cycle/instret to check RV32 high halves.
    emit(csr(1,'hb80,0,8));
    emit(csr(2,'hb80,27,0),1,3);
    emit(csr(1,'hb82,0,8));
    emit(csr(2,'hb82,27,0),1,3);
    while((pc&7)!=0) emit(ri(0,0,0,0));
    // C.JAL +4 differs from RV64 C.ADDIW. Its skipped parcel cannot retire.
    backing[pc]=8'h11; backing[pc+1]=8'h20;
    expected_write[pc]=1; expected[pc]=32'(pc+2);
    backing[pc+2]=8'h00; backing[pc+3]=8'h00; pc+=4;
    // A 32-bit ADDI beginning at byte six crosses a fetch block.
    backing[pc]=8'h01; backing[pc+1]=8'h00; pc+=2;
    emit(ri(0,25,0,77),1,77);
    // Faults from either slot retain the older retirement and raw instruction.
    emit(ld(3,25,24,0)); // LD is not RV32
    emit(ri(0,25,0,78),1,78);
    emit(ri(1,25,8,32)); // RV64 shift-immediate bit 5 is reserved
    emit(ri(0,25,0,79),1,79);
    emit(32'h0014049b); // ADDIW is not RV32
    emit(ri(0,25,0,80),1,80);
    emit(ld(2,25,0,'h1800)); // negative unmapped VA must not alias low memory
    emit(ri(0,25,0,81),1,81);
    split_fault_pc=pc;
    emit(ld(2,25,24,1021)); // 0xffd succeeds first; the second fragment faults at 0x1000.
    emit(ri(0,25,0,82),1,82);
    emit(32'h10500073); // drain into WFI
    // Trap handler checks the faulting encoding/address via architectural CSRs,
    // then advances the saved EPC. Writes are checked separately below.
    put('h800,csr(2,'h341,30,0));
    put('h804,csr(2,'h343,29,0));
    put('h808,ri(0,30,30,4));
    put('h80c,csr(1,'h341,0,30));
    put('h810,32'h30200073);
    repeat(5) @(negedge clock);
    reset=0; start_in.valid=1; start_in.bits='h400;
    @(negedge clock); start_in='0;
    wait(sleeping); repeat(5) @(negedge clock);
    foreach(expected_write[i]) if(expected_write[i]) assert(seen[i]) else $fatal(1,"missing result %h",i);
    assert(faults==5 && dual>0 && completed_count>=8 && ireads>0 && dreads>0 && ureads==2 && uwrites==2) else $fatal(1,"coverage faults=%0d dual=%0d completions=%0d",faults,dual,completed_count);
    $display("RV2Wide RV32 Bare passed: %0d retirements, %0d dual cycles, %0d deferred writes, %0d traps",commits,dual,completed_count,faults);
    $finish;
  end
endmodule
