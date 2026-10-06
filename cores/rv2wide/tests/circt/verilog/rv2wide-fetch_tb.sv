// SPDX-License-Identifier: Apache-2.0
module rv2wide_fetch_tb;
  typedef struct packed { logic [63:0] cause, value; } fault_t;
  typedef struct packed { logic valid; fault_t bits; } fault_flow_t;
  typedef struct packed { logic [63:0] pc; logic [31:0] instruction, raw_instruction; logic [63:0] sequential_pc; logic compressed_illegal; fault_flow_t fault; } instruction_t;
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

  byte unsigned backing[131072], model_bytes[131072];
  logic [63:0] registers[32];
  retirement_t completions[$];
  int cycles=0, reference_pc=0, commits=0, dual_run=0, longest_dual=0;
  int ireads=0, dreads=0, acks=0, replay_count=0, branch_count=0, faults=0, phase=0;
  int wrong_path_reads=0, detached_refills=0, completions_seen=0;
  int reset_canceled_refills=0;
  int compressed_retired=0, straddled_retired=0, compressed_dual_run=0;
  bit iactive=0, dactive=0, wactive=0;
  CHIReqFlit irequest, drequest, wrequest;
  int idue, ipacket, ddue, dpacket;
  int expected_fault_pc, expected_fault_cause;
  logic [63:0] expected_fault_value;
  int ustate=0, udue=0, ureads=0, uwrites=0, fences=0, instruction_fences=0;
  CHIReqFlit urequest;

  function automatic logic [31:0] addi(int rd, rs, imm);
    return {12'(imm),5'(rs),3'b000,5'(rd),7'h13};
  endfunction
  function automatic logic [31:0] jal(int rd, imm);
    return {1'(imm>>20),10'(imm>>1),1'(imm>>11),8'(imm>>12),5'(rd),7'h6f};
  endfunction
  function automatic logic [31:0] bne(int rs1, rs2, imm);
    return {1'(imm>>12),6'(imm>>5),5'(rs2),5'(rs1),3'b001,4'(imm>>1),1'(imm>>11),7'h63};
  endfunction
  function automatic logic [31:0] load(int rd, rs1, offset, width);
    return {12'(offset),5'(rs1),3'(width),5'(rd),7'h03};
  endfunction
  function automatic logic [31:0] store(int rs2, rs1, offset, width);
    return {7'(offset>>5),5'(rs2),5'(rs1),3'(width),5'(offset),7'h23};
  endfunction
  task automatic insn(int pc, logic [31:0] word);
    for (int b=0;b<4;b++) backing[pc+b]=word[b*8+:8];
  endtask
  task automatic parcel(int pc, logic [15:0] word);
    for(int b=0;b<2;b++) backing[pc+b]=word[b*8+:8];
  endtask
  function automatic logic [15:0] c_imm(int funct3, rd, value);
    return {3'(funct3),1'(value>>5),5'(rd),5'(value),2'b01};
  endfunction
  // Independent expansion only for encodings authored by this fixture. The
  // shared expander's catalog fixtures cover the remaining C instruction forms.
  function automatic logic [31:0] expand(logic [15:0] c);
    int rd, rs, value;
    rd=int'(c[11:7]); rs=8+int'(c[9:7]);
    value=int'($signed({c[12],c[6:2]}));
    case({c[15:13],c[1:0]})
      5'b00001: return addi(rd,rd,value);
      5'b01001: return addi(rd,0,value);
      5'b01100: return load(8+int'(c[4:2]),rs,int'({c[6:5],c[12:10],3'b000}),3);
      5'b11100: return store(8+int'(c[4:2]),rs,int'({c[6:5],c[12:10],3'b000}),3);
      5'b10101: return jal(0,int'($signed({c[12],c[8],c[10:9],c[6],c[7],c[2],c[11],c[5:3],1'b0})));
      5'b11001, 5'b11101: begin
        value=int'($signed({c[12],c[6:5],c[2],c[11:10],c[4:3],1'b0}));
        return bne(rs,0,value) & (c[13] ? 32'hffffffff : ~32'h1000);
      end
      5'b10010: begin
        if(c[6:2]==0 && rd!=0) return {12'd0,5'(rd),3'd0,5'(c[12] ? 1 : 0),7'h67};
        if(!c[12] && c[6:2]!=0) return {7'd0,c[6:2],5'd0,3'd0,5'(rd),7'h33};
      end
      default: begin end
    endcase
    $fatal(1,"unmodeled compressed instruction %h",c);
    return 0;
  endfunction
  function automatic int instruction_pa(int pc);
    if(pc>='h400000 && pc<'h401000) return 'h14000+(pc&'hfff);
    if(pc>='h401000 && pc<'h402000) return 'h16000+(pc&'hfff);
    return pc;
  endfunction
  function automatic logic [31:0] instruction_at(int pc);
    logic [31:0] word;
    for (int b=0;b<4;b++) word[b*8+:8]=backing[instruction_pa(pc+b)];
    return word;
  endfunction

  // Public CHI transactions alone drive the byte-addressed backing store.
  // Instruction and data identities have independent retained transactions.
  always_comb begin
    uncached_chi_in='0;
    uncached_chi_in.req.ready=ustate==0 && cycles%7!=0;
    uncached_chi_in.rsp.requester.ready=1;
    uncached_chi_in.dat.request.ready=ustate==3 && cycles%3!=0;
    if(ustate==1 && cycles>=udue) begin
      uncached_chi_in.dat.response.valid=1;
      uncached_chi_in.dat.response.bits.opcode=4'h4;
      uncached_chi_in.dat.response.bits.src_id=7'd5;
      uncached_chi_in.dat.response.bits.home_nid_or_pbha_or_mismatched_mecid=7'd5;
      uncached_chi_in.dat.response.bits.tgt_id=7'd4;
      uncached_chi_in.dat.response.bits.txn_id=urequest.txn_id;
      uncached_chi_in.dat.response.bits.byte_enable='1;
      for(int b=0;b<16;b++) uncached_chi_in.dat.response.bits.data[b*8+:8]=backing[(int'(urequest.address)&~15)+b];
    end
    if((ustate==2 || ustate==4) && cycles>=udue) begin
      uncached_chi_in.rsp.response.valid=1;
      uncached_chi_in.rsp.response.bits.opcode=ustate==2 ? 5'h06 : 5'h04;
      uncached_chi_in.rsp.response.bits.src_id=7'd5;
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
    logic [31:0] word, raw;
    logic [63:0] value, address;
    logic signed [63:0] imm;
    retirement_t expected;
    bit write_rd;
    int rd, rs1, rs2, bytes, length;
    assert((phase==0 || phase>=4) && got.fetched.pc==64'(reference_pc) && !got.fetched.fault.valid)
      else $fatal(1,"retired wrong path/fault pc=%h expected=%h phase=%0d",got.fetched.pc,reference_pc,phase);
    raw=instruction_at(reference_pc);
    length=raw[1:0]==3 ? 4 : 2;
    if(length==2) begin raw={16'd0,raw[15:0]}; compressed_retired++; end
    else if((reference_pc&7)==6) straddled_retired++;
    word=length==2 ? expand(raw[15:0]) : raw;
    assert(got.fetched.raw_instruction==raw && got.fetched.sequential_pc==64'(reference_pc+length) && !got.fetched.compressed_illegal)
      else $fatal(1,"instruction encoding/length at %h",got.fetched.pc);
    assert(got.fetched.instruction==word) else $fatal(1,"fetch payload at %h",got.fetched.pc);
    rd=int'(word[11:7]); rs1=int'(word[19:15]); rs2=int'(word[24:20]);
    value=0; write_rd=0; reference_pc+=length;
    case(word[6:0])
      7'h13: begin
        value=word[14:12]==1 ? registers[rs1]<<word[25:20] : registers[rs1]+64'($signed(word[31:20]));
        write_rd=rd!=0;
      end
      7'h37: begin value=64'($signed({word[31:12],12'b0})); write_rd=rd!=0; end
      7'h33: begin value=registers[rs1]+registers[rs2]; write_rd=rd!=0; end
      7'h67: begin
        value=64'(reference_pc); write_rd=rd!=0;
        reference_pc=int'((registers[rs1]+64'($signed(word[31:20])))&~64'd1);
      end
      7'h6f: begin
        value=64'(reference_pc); write_rd=rd!=0;
        imm=64'($signed({word[31],word[19:12],word[20],word[30:21],1'b0}));
        reference_pc=int'(got.fetched.pc+imm);
      end
      7'h63: begin
        imm=64'($signed({word[31],word[7],word[30:25],word[11:8],1'b0}));
        if((registers[rs1]==registers[rs2]) == (word[14:12]==0)) reference_pc=int'(got.fetched.pc+imm);
      end
      7'h03: begin
        address=registers[rs1]+64'($signed(word[31:20])); write_rd=rd!=0;
        assert(ustate==0) else $fatal(1,"younger load bypassed ordered IO");
        if(phase>=6 && address >= 'h500000 && address < 'h501000) address=(phase==13 ? 'h2000 : 'h15000)+(address & 'hfff);
        bytes=1<<word[13:12];
        for(int b=0;b<bytes;b++) value[b*8+:8]=model_bytes[int'(address)+b];
        if(!word[14] && value[bytes*8-1]) value|='1 << (bytes*8);
      end
      7'h23: begin
        imm=64'($signed({word[31:25],word[11:7]})); address=registers[rs1]+imm;
        assert(ustate==0) else $fatal(1,"younger store bypassed ordered IO");
        if(phase>=6 && address >= 'h500000 && address < 'h501000) address=(phase==13 ? 'h2000 : 'h15000)+(address & 'hfff);
        for(int b=0;b<(1<<word[13:12]);b++) model_bytes[int'(address)+b]=registers[rs2][b*8+:8];
      end
      7'h0f: begin
        assert(ustate==0 && !dactive && completions.size()==0) else $fatal(1,"fence before memory drain");
        fences++;
        if(word[14:12]==1) instruction_fences++;
      end
      7'h73: begin
        assert(phase>=5) else $fatal(1,"unexpected system instruction");
        if(phase>=6) begin
          if(word==32'h30200073) reference_pc=phase==19 ? expected_fault_pc+4 : 'h400000;
          else if(word[14:12]==2) begin
            write_rd=rd!=0;
            case(word[31:20])
              12'h342: value=64'(expected_fault_cause);
              12'h343: value=expected_fault_value;
              12'h341: value=64'(expected_fault_pc);
              default: $fatal(1,"unexpected paged CSR read");
            endcase
          end
        end else case(got.fetched.pc)
          'h304, 'h38c, 'h310: begin end // CSRRW x0 and WFI
          'h380: begin value=11; write_rd=1; end // mcause
          'h384: begin value='h308; write_rd=1; end // mepc
          'h390: reference_pc='h30c; // MRET
          default: $fatal(1,"unexpected CSR PC");
        endcase
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
    if(retired[0].valid && retired[1].valid) begin
      dual_run++; if(dual_run>longest_dual) longest_dual=dual_run;
      if(phase==14 && dual_run>compressed_dual_run) compressed_dual_run=dual_run;
    end
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
          assert(redirect.bits.target==(phase>=5 ? 'h380 : 0) && !halted) else $fatal(1,"trap did not target mtvec");
          if(phase==0) assert(reference_pc==4096 && registers[30]==77) else $fatal(1,"lost page-end instruction");
          if(phase==4) assert(reference_pc==652) else $fatal(1,"lost older load or retired after illegal instruction");
          if(phase>=5) reference_pc=int'(redirect.bits.target);
          faults++;
        end
        3: assert(phase>=5) else $fatal(1,"unexpected system recovery");
      endcase
    end
    if(instruction_chi_out.req.valid && instruction_chi_in.req.ready) begin
      assert(instruction_chi_out.req.bits.opcode==7'h03 && (instruction_chi_out.req.bits.address<4096 || (instruction_chi_out.req.bits.address>='h10000 && instruction_chi_out.req.bits.address<'h20000)) && instruction_chi_out.req.bits.address[5:0]==0)
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
      assert(ustate==0) else $fatal(1,"cache request bypassed IO");
      assert(data_chi_out.requests.bits.address<4096 || (data_chi_out.requests.bits.address>='h10000 && data_chi_out.requests.bits.address<'h20000)) else $fatal(1,"unmapped data transaction");
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
    if(uncached_chi_out.req.valid && uncached_chi_in.req.ready) begin
      assert(!dactive && !wactive && (uncached_chi_out.req.bits.opcode==7'h04 || uncached_chi_out.req.bits.opcode==7'h1c)) else $fatal(1,"IO failed to drain cached predecessors");
      assert(uncached_chi_out.req.bits.address>='h2000 && uncached_chi_out.req.bits.address<'h2400) else $fatal(1,"wrong IO address");
      assert(uncached_chi_out.req.bits.size_or_num_req<=3) else $fatal(1,"IO widened beyond XLEN");
      case(uncached_chi_out.req.bits.address)
        'h2008, 'h2009, 'h2203: assert(uncached_chi_out.req.bits.size_or_num_req==0) else $fatal(1,"byte IO widened");
        'h200a: assert(uncached_chi_out.req.bits.size_or_num_req==1) else $fatal(1,"halfword IO width");
        'h200c, 'h2300: assert(uncached_chi_out.req.bits.size_or_num_req==2) else $fatal(1,"word IO width");
        'h2010, 'h20f0: assert(uncached_chi_out.req.bits.size_or_num_req==3) else $fatal(1,"doubleword IO width");
        default: $fatal(1,"unexpected or wrong-path IO transaction");
      endcase
      assert(uncached_chi_out.req.bits.mem_attr[1]==(uncached_chi_out.req.bits.address<'h2300)) else $fatal(1,"wrong Device attribute");
      urequest<=uncached_chi_out.req.bits;
      if(uncached_chi_out.req.bits.opcode==7'h04) begin ustate<=1; ureads++; end
      else begin ustate<=2; uwrites++; end
      udue<=cycles+40;
    end
    if(uncached_chi_in.dat.response.valid && uncached_chi_out.dat.response.ready) ustate<=0;
    if(uncached_chi_in.rsp.response.valid && uncached_chi_out.rsp.response.ready) ustate<=ustate==2 ? 3 : 0;
    if(uncached_chi_out.dat.request.valid && uncached_chi_in.dat.request.ready) begin
      logic [15:0] expected_mask;
      expected_mask=16'((1<<(1<<urequest.size_or_num_req))-1)<<urequest.address[3:0];
      assert(uncached_chi_out.dat.request.bits.byte_enable==expected_mask) else $fatal(1,"IO byte enables do not match exact request width/address");
      for(int b=0;b<16;b++) if(expected_mask[b]) begin
        int address=(int'(urequest.address)&~15)+b;
        assert(uncached_chi_out.dat.request.bits.data[b*8+:8]==model_bytes[address]) else $fatal(1,"IO store lane mismatch");
        backing[address]=uncached_chi_out.dat.request.bits.data[b*8+:8];
      end
      // A device command publishes externally written code before its completion.
      // The old instruction line is already resident; FENCE.I must discard it.
      if(phase==12 && urequest.address=='h20f0) insn('h700,addi(20,0,77));
      ustate<=4; udue<=cycles+40;
    end
  end

  task automatic launch(int address, cause, fault_pc, logic [63:0] fault_value='1);
    int before_faults=faults;
    @(negedge clock);
    assert(halted) else $fatal(1,"start before halt");
    expected_fault_pc=fault_pc; expected_fault_cause=cause;
    expected_fault_value=fault_value=='1 ? 64'(fault_pc) : fault_value;
    start_in='{valid:1'b1,bits:64'(address)};
    @(negedge clock); start_in='0;
    wait(faults>before_faults);
    // The real core resumes at mtvec now. End this scenario with a coordinated
    // reset before executing the handler, including the external CHI epoch.
    @(negedge clock); reset=1;
    reset_canceled_refills=ireads-acks;
    iactive=0; dactive=0; wactive=0; ustate=0;
    for(int r=0;r<32;r++) registers[r]=0;
    repeat(3) @(negedge clock);
    reset=0;
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
    phase=2; launch(515,0,515); // odd PC faults without issuing an aligned read
    phase=3; launch(4096,1,4096); // unmapped restart never reaches CHI
    phase=4; reference_pc=640; launch(640,2,652,64'hffffffff);
    assert(faults==5 && acks+reset_canceled_refills==ireads) else $fatal(1,"lost fault or retained acknowledgement faults=%0d ack=%0d read=%0d",faults,acks,ireads);
    // Execute a real handler through L1I: program mtvec, take ECALL, read
    // architectural trap state, advance mepc, return, then sleep after a marker.
    phase=5; reference_pc='h300;
    insn('h300,addi(1,0,'h380));
    insn('h304,{12'h305,5'd1,3'b001,5'd0,7'h73});
    insn('h308,32'h00000073); insn('h30c,addi(12,0,99)); insn('h310,32'h10500073);
    insn('h380,{12'h342,5'd0,3'b010,5'd10,7'h73});
    insn('h384,{12'h341,5'd0,3'b010,5'd11,7'h73});
    insn('h388,addi(11,11,4));
    insn('h38c,{12'h341,5'd11,3'b001,5'd0,7'h73});
    insn('h390,32'h30200073);
    expected_fault_pc='h308; expected_fault_cause=11; expected_fault_value=0;
    @(negedge clock); start_in='{valid:1'b1,bits:64'h300};
    @(negedge clock); start_in='0;
    wait(sleeping); repeat(3) @(negedge clock);
    assert(reference_pc=='h314 && registers[10]==11 && registers[11]=='h30c && registers[12]==99 && faults==6)
      else $fatal(1,"trap handler/return execution failed");
    // Real M-mode setup and MRET into translated S-mode. The same three-level
    // tables serve independent I/D TLBs through the coherent data cache.
    @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0;
    for(int r=0;r<32;r++) registers[r]=0;
    for(int p='h10000;p<'h20000;p++) begin backing[p]=0; model_bytes[p]=0; end
    begin
      logic [63:0] pte;
      pte=('h11<<10)|1; for(int b=0;b<8;b++) backing['h10000+b]=pte[b*8+:8];
      pte=('h12<<10)|1; for(int b=0;b<8;b++) backing['h11010+b]=pte[b*8+:8];
      pte=('h14<<10)|'hcb; for(int b=0;b<8;b++) backing['h12000+b]=pte[b*8+:8];
      pte=('h15<<10)|'hc7; for(int b=0;b<8;b++) backing['h12800+b]=pte[b*8+:8];
      for(int b=0;b<16;b++) begin backing['h15000+b]=8'(b+1); model_bytes['h15000+b]=8'(b+1); end
    end
    insn('h300,addi(1,0,'h380)); insn('h304,{12'h305,5'd1,3'b001,5'd0,7'h73});
    insn('h308,addi(1,0,8)); insn('h30c,{6'd0,6'd60,5'd1,3'b001,5'd1,7'h13});
    insn('h310,addi(1,1,16)); insn('h314,{12'h180,5'd1,3'b001,5'd0,7'h73});
    insn('h318,32'h12000073); // SFENCE.VMA
    insn('h31c,addi(1,0,2047)); insn('h320,addi(1,1,1));
    insn('h324,{12'h300,5'd1,3'b001,5'd0,7'h73}); // MPP=S
    insn('h328,{20'h400,5'd1,7'h37}); insn('h32c,{12'h341,5'd1,3'b001,5'd0,7'h73});
    insn('h330,32'h30200073);
    insn('h14000,{20'h500,5'd1,7'h37});
    insn('h14004,{12'd0,5'd1,3'b011,5'd5,7'h03});
    insn('h14008,addi(6,5,1)); insn('h1400c,{7'd0,5'd6,5'd1,3'b011,5'd8,7'h23});
    insn('h14010,{12'd8,5'd1,3'b011,5'd7,7'h03}); insn('h14014,32'h12000073);
    insn('h14018,{12'd8,5'd1,3'b011,5'd8,7'h03}); insn('h1401c,{20'h501,5'd1,7'h37});
    insn('h14020,addi(12,0,77)); insn('h14024,{12'd0,5'd1,3'b011,5'd9,7'h03});
    insn('h380,{12'h342,5'd0,3'b010,5'd10,7'h73});
    insn('h384,{12'h343,5'd0,3'b010,5'd11,7'h73});
    insn('h388,{12'h341,5'd0,3'b010,5'd13,7'h73}); insn('h38c,32'h10500073);
    phase=6; reference_pc='h300; expected_fault_pc='h400024; expected_fault_cause=13; expected_fault_value='h501000;
    repeat(3) @(negedge clock); reset=0;
    @(negedge clock); start_in='{valid:1'b1,bits:64'h300};
    @(negedge clock); start_in='0;
    wait(sleeping); repeat(3) @(negedge clock);
    assert(registers[5]==64'h0807060504030201 && registers[7]==registers[6] && registers[8]==registers[6] && registers[12]==77 && registers[10]==13 && registers[11]=='h501000 && registers[13]=='h400024 && faults==7)
      else $fatal(1,"Sv39 execution, SFENCE, or precise page fault failed");
    for(int scenario=7;scenario<=8;scenario++) begin
      @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0;
      for(int r=0;r<32;r++) registers[r]=0;
      for(int b=0;b<16;b++) begin backing['h15000+b]=8'(b+1); model_bytes['h15000+b]=8'(b+1); end
      if(scenario==7) begin
        insn('h14020,{7'd0,5'd6,5'd1,3'b011,5'd0,7'h23});
        insn('h14024,addi(12,0,88)); // younger result must not retire
        expected_fault_pc='h400020; expected_fault_cause=15; expected_fault_value='h501000;
      end else begin
        insn('h14020,addi(12,0,77)); insn('h14024,jal(0,'h1000-'h24));
        expected_fault_pc='h401000; expected_fault_cause=12; expected_fault_value='h401000;
      end
      phase=scenario; reference_pc='h300;
      repeat(3) @(negedge clock); reset=0;
      @(negedge clock); start_in='{valid:1'b1,bits:64'h300};
      @(negedge clock); start_in='0;
      wait(sleeping); repeat(3) @(negedge clock);
      assert(registers[10]==64'(expected_fault_cause) && registers[11]==expected_fault_value && registers[13]==64'(expected_fault_pc) && registers[12]==(scenario==7 ? 0 : 77) && faults==scenario+1)
        else $fatal(1,"Sv39 store/fetch trap provenance or younger squash failed");
    end
    // Exact-width MMIO, ordinary uncached RAM, and cache-hit ordering. A taken
    // branch skips a device store; a byte-only mapping must not become an 8B read.
    @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0;
    for(int r=0;r<32;r++) registers[r]=0;
    for(int b='h2000;b<'h2400;b++) begin backing[b]=8'(b); model_bytes[b]=8'(b); end
    insn('h400,{20'h2,5'd1,7'h37}); insn('h404,addi(2,0,-2));
    insn('h408,addi(3,0,2047)); insn('h40c,addi(3,3,1));
    insn('h410,load(4,3,0,3)); // older cold cached read
    for(int width=0;width<4;width++) begin
      int pc='h414+width*16;
      insn(pc,store(2,1,8+(1<<width),width));
      insn(pc+4,load(5+width,1,8+(1<<width),width));
      insn(pc+8,load(9+width,3,0,3)); // younger warm cache hit
      insn(pc+12,32'h0ff0000f);
    end
    insn('h454,load(13,1,'h203,4));
    insn('h458,store(2,1,'h300,2)); insn('h45c,load(14,1,'h300,6));
    insn('h460,jal(0,8)); insn('h464,store(2,1,0,3));
    insn('h468,32'h10500073);
    phase=9; reference_pc='h400;
    repeat(3) @(negedge clock); reset=0;
    @(negedge clock); start_in='{valid:1'b1,bits:64'h400};
    @(negedge clock); start_in='0;
    wait(sleeping); repeat(3) @(negedge clock);
    assert(ureads==6 && uwrites==5 && fences==4 && registers[13]==3 && registers[14]==64'hfffffffe) else $fatal(1,"IO/uncached/fence coverage");
    for(int r=5;r<=8;r++) assert(registers[r]==64'hfffffffffffffffe) else $fatal(1,"IO signed load lane");
    // Permission and missing-mapping faults have no device-side effects.
    for(int scenario=10;scenario<=11;scenario++) begin
      @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0;
      for(int r=0;r<32;r++) registers[r]=0;
      insn('h400,addi(1,0,'h380)); insn('h404,{12'h305,5'd1,3'b001,5'd0,7'h73});
      insn('h408,{20'h2,5'd1,7'h37});
      insn('h40c,scenario==10 ? store(0,1,'h100,3) : load(5,1,'h204,4));
      expected_fault_pc='h40c; expected_fault_cause=scenario==10 ? 7 : 5;
      expected_fault_value=scenario==10 ? 'h2100 : 'h2204;
      phase=scenario; reference_pc='h400;
      repeat(3) @(negedge clock); reset=0;
      @(negedge clock); start_in='{valid:1'b1,bits:64'h400};
      @(negedge clock); start_in='0;
      wait(sleeping); repeat(3) @(negedge clock);
      assert(ureads==6 && uwrites==5 && registers[10]==64'(expected_fault_cause) && registers[11]==expected_fault_value) else $fatal(1,"MMIO fault leaked request or lost provenance");
    end
    @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0;
    for(int r=0;r<32;r++) registers[r]=0;
    insn('h400,addi(10,0,0)); insn('h404,jal(0,'h700-'h404));
    insn('h700,addi(20,0,1)); insn('h704,bne(10,0,12));
    insn('h708,jal(0,'h440-'h708)); insn('h710,32'h10500073);
    insn('h440,{20'h2,5'd1,7'h37}); insn('h444,addi(2,0,1));
    insn('h448,store(2,1,'hf0,3)); insn('h44c,addi(10,0,1));
    insn('h450,32'h0000100f); insn('h454,jal(0,'h700-'h454));
    phase=12; reference_pc='h400;
    repeat(3) @(negedge clock); reset=0;
    @(negedge clock); start_in='{valid:1'b1,bits:64'h400};
    @(negedge clock); start_in='0;
    wait(sleeping); repeat(3) @(negedge clock);
    assert(registers[20]==77 && instruction_fences==1 && uwrites==6) else $fatal(1,"FENCE.I did not observe published code");
    // Route translated S-mode accesses by PA, but preserve the VA on PMA faults.
    @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0;
    for(int r=0;r<32;r++) registers[r]=0;
    begin
      logic [63:0] pte=('h2<<10)|'hc7;
      for(int b=0;b<8;b++) backing['h12800+b]=pte[b*8+:8];
    end
    insn('h14000,{20'h500,5'd1,7'h37}); insn('h14004,addi(6,0,-2));
    insn('h14008,store(6,1,8,0)); insn('h1400c,load(7,1,8,0));
    insn('h14010,store(6,1,'h100,0)); insn('h14014,addi(12,0,88));
    expected_fault_pc='h400010; expected_fault_cause=7; expected_fault_value='h500100;
    phase=13; reference_pc='h300;
    repeat(3) @(negedge clock); reset=0;
    @(negedge clock); start_in='{valid:1'b1,bits:64'h300};
    @(negedge clock); start_in='0;
    wait(sleeping); repeat(3) @(negedge clock);
    assert(registers[7]==64'hfffffffffffffffe && registers[11]=='h500100 && registers[12]==0 && ureads==7 && uwrites==7) else $fatal(1,"translated IO/VA fault ordering");
    // Four C instructions per block, all restart offsets, and 32-bit suffixes
    // across both eight-byte blocks and a cold 64-byte line. The warm loop must
    // still retire two per cycle; delayed C.LD fills the buffers behind it.
    @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0;
    for(int r=0;r<32;r++) registers[r]=0;
    for(int b=2048;b<2064;b++) begin backing[b]=8'(b); model_bytes[b]=8'(b); end
    insn('h600,addi(31,0,3)); parcel('h604,c_imm(2,8,7)); parcel('h606,c_imm(2,9,9));
    for(int p='h608;p<'h638;p+=2) parcel(p,c_imm(2,10+((p/2)&1),(p/2)&31));
    parcel('h638,c_imm(0,31,-1)); insn('h63a,bne(31,0,'h608-'h63a));
    insn('h63e,addi(20,20,1)); parcel('h642,c_imm(2,8,5)); parcel('h644,c_imm(2,9,9));
    insn('h646,addi(21,20,2)); insn('h64a,addi(8,0,2047)); insn('h64e,addi(8,8,1));
    parcel('h652,16'h6004); parcel('h654,c_imm(0,9,1)); parcel('h656,16'he404);
    parcel('h658,16'h6408); parcel('h65a,16'h85aa);
    insn('h65c,addi(5,0,'h682)); parcel('h660,16'h9282);
    insn('h662,addi(12,1,0)); insn('h666,addi(14,0,14)); insn('h66a,32'h10500073);
    parcel('h682,c_imm(2,13,13)); parcel('h684,16'hc011); parcel('h686,16'he011);
    parcel('h688,16'h8002); parcel('h68a,16'ha011); parcel('h68c,16'h8002); parcel('h68e,16'h8082);
    phase=14; reference_pc='h600; dual_run=0;
    repeat(3) @(negedge clock); reset=0;
    @(negedge clock); start_in='{valid:1'b1,bits:64'h600};
    @(negedge clock); start_in='0;
    wait(sleeping); repeat(3) @(negedge clock);
    assert(compressed_retired>=80 && compressed_dual_run>=5 && straddled_retired>=3 && registers[20]==1 && registers[21]==3 && registers[9]==registers[10] && registers[10]==registers[11] && registers[12]=='h662 && registers[13]==13 && reference_pc=='h66e)
      else $fatal(1,"compressed stream/link/backpressure coverage count=%0d dual=%0d cross=%0d",compressed_retired,compressed_dual_run,straddled_retired);
    // An illegal compressed encoding retains its 16-bit mtval after an older
    // accepted compressed load drains, without executing its canonical zero.
    @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0;
    for(int r=0;r<32;r++) registers[r]=0;
    insn('h400,addi(1,0,'h380)); insn('h404,{12'h305,5'd1,3'b001,5'd0,7'h73});
    insn('h408,addi(8,0,2047)); insn('h40c,addi(8,8,1));
    parcel('h410,16'h6004); parcel('h412,16'h8002); parcel('h414,c_imm(2,12,9));
    expected_fault_pc='h412; expected_fault_cause=2; expected_fault_value='h8002;
    phase=15; reference_pc='h400;
    repeat(3) @(negedge clock); reset=0;
    @(negedge clock); start_in='{valid:1'b1,bits:64'h400};
    @(negedge clock); start_in='0;
    wait(sleeping); repeat(3) @(negedge clock);
    assert(registers[11]=='h8002 && registers[13]=='h412 && registers[12]==0) else $fatal(1,"compressed illegal trap encoding");
    // A straddling instruction belongs to its first PC, but a continuation-page
    // fault reports the second page. Then install that page and execute it.
    for(int scenario=16;scenario<=17;scenario++) begin
      @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0;
      for(int r=0;r<32;r++) registers[r]=0;
      begin
        logic [63:0] pte=scenario==17 ? ('h16<<10)|'hcb : 0;
        logic [31:0] crossing=addi(12,0,77);
        for(int b=0;b<8;b++) backing['h12008+b]=pte[b*8+:8];
        insn('h14000,jal(0,'hffe));
        parcel('h14ffe,crossing[15:0]); parcel('h16000,crossing[31:16]);
        parcel('h16002,c_imm(2,14,14)); insn('h16004,32'h10500073);
      end
      expected_fault_pc='h400ffe; expected_fault_cause=12; expected_fault_value='h401000;
      phase=scenario; reference_pc='h300;
      repeat(3) @(negedge clock); reset=0;
      @(negedge clock); start_in='{valid:1'b1,bits:64'h300};
      @(negedge clock); start_in='0;
      wait(sleeping); repeat(3) @(negedge clock);
      if(scenario==16) assert(registers[11]=='h401000 && registers[13]=='h400ffe && registers[12]==0) else $fatal(1,"straddling translation fault provenance");
      else assert(registers[12]==77 && registers[14]==14 && reference_pc=='h401008) else $fatal(1,"straddling translation replay lost prefix");
    end
    // The same suffix rule applies to a physical-map access fault.
    @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0;
    for(int r=0;r<32;r++) registers[r]=0;
    insn('h408,jal(0,'hffe-'h408)); parcel('hffe,16'h0613);
    expected_fault_pc='hffe; expected_fault_cause=1; expected_fault_value='h1000;
    phase=18; reference_pc='h400;
    repeat(3) @(negedge clock); reset=0;
    @(negedge clock); start_in='{valid:1'b1,bits:64'h400};
    @(negedge clock); start_in='0;
    wait(sleeping); repeat(3) @(negedge clock);
    assert(registers[11]=='h1000 && registers[13]=='hffe && registers[12]==0) else $fatal(1,"straddling access fault provenance");
    // C.EBREAK traps at a halfword PC; the handler deliberately skips the
    // following C.LI and MRET must preserve bit one in its halfword target.
    @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0;
    for(int r=0;r<32;r++) registers[r]=0;
    parcel('h408,16'h0001); parcel('h40a,16'h9002);
    parcel('h40c,c_imm(2,12,12)); insn('h40e,32'h10500073);
    insn('h38c,addi(13,13,4)); // Return to 0x40e, whose bit one must survive mepc.
    insn('h390,{12'h341,5'd13,3'b001,5'd0,7'h73}); insn('h394,32'h30200073);
    expected_fault_pc='h40a; expected_fault_cause=3; expected_fault_value=0;
    phase=19; reference_pc='h400;
    repeat(3) @(negedge clock); reset=0;
    @(negedge clock); start_in='{valid:1'b1,bits:64'h400};
    @(negedge clock); start_in='0;
    wait(sleeping); repeat(3) @(negedge clock);
    assert(registers[10]==3 && registers[11]==0 && registers[13]=='h40e && registers[12]==0 && reference_pc=='h412) else $fatal(1,"C.EBREAK/halfword MRET");
    $display("RV2Wide fetching core passed: %0d retirements, %0d-cycle dual run, %0d I refills, %0d D refills, %0d faults, %0d IO reads/%0d writes, %0d fences",commits,longest_dual,ireads,dreads,faults,ureads,uwrites,fences);
    $finish;
  end
endmodule
