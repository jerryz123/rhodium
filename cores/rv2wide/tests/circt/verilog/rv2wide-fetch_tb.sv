// Checks production fetch, selected compressed subsets, prediction, and precise memory/fault recovery.
// SPDX-License-Identifier: Apache-2.0
module rv2wide_fetch_tb;
`ifndef BPRED_DISABLED
  import "DPI-C" function void rv2wide_fetch_trace_bind();
  import "DPI-C" function void rv2wide_fetch_trace_finish();
  import "DPI-C" function void rv2wide_fetch_trace_expect(input int unsigned lane, input longint unsigned pc, input int unsigned instruction, prediction, ras_mismatch);
  import "DPI-C" function void rv2wide_fetch_trace_check(input int unsigned reset);
  logic trace_reset=1;
  always @(posedge clock) trace_reset<=reset;
  always @(negedge clock) rv2wide_fetch_trace_check(int'(trace_reset));
`endif
  typedef struct packed { logic [63:0] cause, value; } fault_t;
  typedef struct packed { logic valid; fault_t bits; } fault_flow_t;
  typedef struct packed { logic valid; logic [63:0] pc, target; logic compressed; logic [1:0] ras_action; } prediction_t;
  typedef struct packed { logic [63:0] pc; logic [31:0] instruction, raw_instruction; logic [63:0] sequential_pc; logic compressed_illegal; fault_flow_t fault; prediction_t prediction; logic [1:0] speculated_ras_action; } instruction_t;
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
  int predicted_branches=0, predicted_conditional=0, predicted_straddles=0;
  int compressed_retired=0, straddled_retired=0, compressed_dual_run=0;
  int zc_pairs=0;
  int instruction_prefetch_reads=0;
  bit iactive=0, dactive=0, wactive=0;
  CHIReqFlit irequest, drequest, wrequest;
  int idue, ipacket, ddue, dpacket;
  int expected_fault_pc, expected_fault_cause;
  logic [63:0] expected_fault_value;
  int ustate=0, udue=0, ureads=0, uwrites=0, fences=0, instruction_fences=0;
  int pauses=0, last_pause_cycle=0;
  CHIReqFlit urequest;
  bit reservation_valid=0;
  logic [63:0] reservation_address;
  int reservation_width;

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
  function automatic logic [31:0] atomic_insn(int operation, width, rd, rs1, rs2=0);
    return {5'(operation),2'b11,5'(rs2),5'(rs1),3'(width),5'(rd),7'h2f};
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
  function automatic logic [15:0] c_zcb_unary(int rd, operation);
    return {6'h27,3'(rd-8),5'(operation),2'b01};
  endfunction
  function automatic logic [15:0] c_zcb_memory(int operation, rd, base, offset, bit signed_half=0);
    return {6'(operation),3'(base-8),1'(operation=='h20 || operation=='h22 ? offset : int'(signed_half)),1'(offset>>1),3'(rd-8),2'b00};
  endfunction
  // Independent expansion only for encodings authored by this fixture. The
  // shared expander's catalog fixtures cover the remaining C instruction forms.
  function automatic logic [31:0] expand(logic [15:0] c);
    int rd, rs, value;
    rd=int'(c[11:7]); rs=8+int'(c[9:7]);
    value=int'($signed({c[12],c[6:2]}));
    if(c[1:0]==0) case(c[15:10])
      'h20: return load(8+int'(c[4:2]),rs,int'({c[5],c[6]}),4);
      'h21: return load(8+int'(c[4:2]),rs,int'({c[5],1'b0}),c[6] ? 1 : 5);
      'h22: return store(8+int'(c[4:2]),rs,int'({c[5],c[6]}),0);
      'h23: return store(8+int'(c[4:2]),rs,int'({c[5],1'b0}),1);
      default: begin end
    endcase
    if(c[1:0]==1 && c[15:10]=='h27) begin
      if(c[6:5]==2) return {7'd1,5'(8+int'(c[4:2])),5'(rs),3'd0,5'(rs),7'h33};
      case(c[6:2])
        'h18: return {12'h0ff,5'(rs),3'd7,5'(rs),7'h13};
        'h19: return {12'h604,5'(rs),3'd1,5'(rs),7'h13};
        'h1a: return {12'h080,5'(rs),3'd4,5'(rs),7'h3b};
        'h1b: return {12'h605,5'(rs),3'd1,5'(rs),7'h13};
        'h1c: return {7'h04,5'd0,5'(rs),3'd0,5'(rs),7'h3b};
        'h1d: return {12'hfff,5'(rs),3'd4,5'(rs),7'h13};
        default: begin end
      endcase
    end
    if((c&16'hf87f)==16'h6001 && c[11:7]<16 && c[7]) return addi(0,0,0);
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
  function automatic int data_pa(logic [63:0] address);
    if(phase>=6 && address>='h500000 && address<'h501000) return (phase==13 ? 'h2000 : 'h15000)+int'(address&'hfff);
    if(phase>=23 && address>='h501000 && address<'h502000) return 'h17000+int'(address&'hfff);
    return int'(address);
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

  task automatic retire(retirement_t got, int lane);
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
        if(word[31:26]=='h0a && word[14:12]==1) value=registers[rs1]|(64'd1<<word[25:20]);
        else if(word[31:20]=='h6b8 && word[14:12]==5)
          for(int b=0;b<8;b++) value[b*8+:8]=registers[rs1][(7-b)*8+:8];
        else if(word[31:20]=='h604 && word[14:12]==1) value=64'($signed(registers[rs1][7:0]));
        else if(word[31:20]=='h605 && word[14:12]==1) value=64'($signed(registers[rs1][15:0]));
        else case(word[14:12])
          1: value=registers[rs1]<<word[25:20];
          4: value=registers[rs1]^64'($signed(word[31:20]));
          7: value=registers[rs1]&64'($signed(word[31:20]));
          default: value=registers[rs1]+64'($signed(word[31:20]));
        endcase
        write_rd=rd!=0;
      end
      7'h37: begin value=64'($signed({word[31:12],12'b0})); write_rd=rd!=0; end
      7'h33,7'h3b: begin
        if(word[31:25]==1) case(word[14:12])
          0: value=registers[rs1]*registers[rs2];
          4: value=registers[rs2]==0 ? '1 : $unsigned($signed(registers[rs1])/$signed(registers[rs2]));
          5: value=registers[rs2]==0 ? '1 : registers[rs1]/registers[rs2];
          default: $fatal(1,"unmodeled fetching M instruction");
        endcase
        else if(word[31:25]=='h10 && word[14:12]==4) value=(registers[rs1]<<2)+registers[rs2];
        else value=registers[rs1]+registers[rs2];
        if(word[31:25]=='h04 && word[6:0]==7'h3b)
          value=word[14:12]==4 ? {48'd0,registers[rs1][15:0]} : {32'd0,registers[rs1][31:0]}+registers[rs2];
        else if(word[6:0]==7'h3b) value={{32{value[31]}},value[31:0]};
        write_rd=rd!=0;
      end
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
        bytes=1<<word[13:12];
        for(int b=0;b<bytes;b++) value[b*8+:8]=model_bytes[data_pa(address+64'(b))];
        if(!word[14] && value[bytes*8-1]) value|='1 << (bytes*8);
      end
      7'h23: begin
        imm=64'($signed({word[31:25],word[11:7]})); address=registers[rs1]+imm;
        assert(ustate==0) else $fatal(1,"younger store bypassed ordered IO");
        for(int b=0;b<(1<<word[13:12]);b++) model_bytes[data_pa(address+64'(b))]=registers[rs2][b*8+:8];
      end
      7'h2f: begin
        logic [63:0] replacement;
        bit success;
        address=registers[rs1]; bytes=1<<word[13:12];
        for(int b=0;b<bytes;b++) value[b*8+:8]=model_bytes[int'(address)+b];
        if(bytes==4) value={{32{value[31]}},value[31:0]};
        replacement=registers[rs2];
        case(word[31:27])
          2: begin reservation_valid=1; reservation_address=address; reservation_width=int'(word[14:12]); end
          3: begin
            success=reservation_valid && address==reservation_address && int'(word[14:12])==reservation_width;
            value=success ? 0 : 1; reservation_valid=0;
          end
          0: begin replacement=value+registers[rs2]; reservation_valid=0; end
          1: reservation_valid=0;
          default: $fatal(1,"unmodeled fetching AMO");
        endcase
        if(word[31:27]!=2 && (word[31:27]!=3 || success))
          for(int b=0;b<bytes;b++) model_bytes[int'(address)+b]=replacement[b*8+:8];
        write_rd=rd!=0;
        assert(got.deferred && ustate==0) else $fatal(1,"atomic authorization/order");
      end
      7'h0f: begin
        if(word==32'h0100000f) begin
          assert(!got.write && !got.deferred) else $fatal(1,"PAUSE created a write");
          pauses++; last_pause_cycle=cycles;
        end else begin
        assert(ustate==0 && !dactive && completions.size()==0) else $fatal(1,"fence before memory drain");
        fences++;
        if(word[14:12]==1) instruction_fences++;
        end
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
`ifndef BPRED_DISABLED
    begin
      bit is_branch, pushes, pops;
      logic [63:0] predicted_pc;
      int unsigned action, prediction;
      is_branch=word[6:0] inside {7'h63,7'h6f,7'h67};
      pushes=(word[6:0] inside {7'h6f,7'h67}) && (rd==1 || rd==5);
      pops=word[6:0]==7'h67 && (rs1==1 || rs1==5) && (!pushes || rd!=rs1);
      action=pushes ? (pops ? 3 : 1) : (pops ? 2 : 0);
      predicted_pc=got.fetched.prediction.valid ? got.fetched.prediction.target : got.fetched.sequential_pc;
      prediction=is_branch ? (predicted_pc==64'(reference_pc) ? 1 : 2) : 0;
      rv2wide_fetch_trace_expect(lane,got.fetched.pc,raw,prediction,int'(got.fetched.speculated_ras_action!=2'(action)));
    end
`endif
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
    if(cycles>25000) $fatal(1,"timeout phase=%0d pc=%h",phase,reference_pc);
    if(retired[0].valid && retired[1].valid) begin
      dual_run++; if(dual_run>longest_dual) longest_dual=dual_run;
      if(phase==14 && dual_run>compressed_dual_run) compressed_dual_run=dual_run;
      if(phase==26) zc_pairs++;
    end
    else dual_run=0;
    for(int lane=0;lane<2;lane++) if(retired[lane].valid) begin
      if(retired[lane].bits.fetched.prediction.valid) begin
        predicted_branches++;
        if(retired[lane].bits.fetched.instruction[6:0]==7'h63) predicted_conditional++;
        if(phase==14 && retired[lane].bits.fetched.pc=='h63e) predicted_straddles++;
      end
      retire(retired[lane].bits,lane);
    end
    if(completed.valid) begin
      retirement_t expected;
      int index;
      index=-1;
      assert(completions.size()>0) else $fatal(1,"orphan completion");
      foreach(completions[i]) if(completions[i].fetched.pc==completed.bits.fetched.pc) index=i;
      assert(index>=0) else $fatal(1,"completion instruction identity");
      expected=completions[index]; completions.delete(index);
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
          if(phase==25) for(int b=0;b<3;b++) model_bytes['h15ffd+b]=registers[2][b*8+:8];
          faults++;
        end
        3: assert(phase>=5) else $fatal(1,"unexpected system recovery");
      endcase
    end
    if(instruction_chi_out.req.valid && instruction_chi_in.req.ready) begin
      if(phase==28 && instruction_chi_out.req.bits.address=='hc00) begin
        assert(registers[2]>0) else $fatal(1,"instruction prefetch did not arrive before the branch");
        instruction_prefetch_reads++;
      end
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
        int address;
        address=(int'(urequest.address)&~15)+b;
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
`ifndef BPRED_DISABLED
    rv2wide_fetch_trace_bind();
`endif
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
    insn(184,addi(5,0,316));
    insn(188,{12'd0,5'd5,3'd0,5'd21,7'h67}); // cold indirect target: discard erroneous younger line 192
    insn(316,addi(22,21,1));
    insn(320,jal(0,4092-320));
    insn(4092,addi(30,0,77));
    // An older cold load must finish before the younger illegal instruction
    // reports its fault. Fetch cancellation happens before that delayed report.
    insn(640,addi(1,0,2047)); insn(644,addi(1,1,65));
    insn(648,{12'd0,5'd1,3'b011,5'd16,7'h03}); insn(652,32'hffffffff);
    repeat(4) @(negedge clock); reset=0;
    launch(0,1,4096);
    assert(longest_dual>=5 && dreads>0 && completions_seen>0 && branch_count>=3)
      else $fatal(1,"missing throughput/memory coverage dual=%0d dreads=%0d complete=%0d branches=%0d",longest_dual,dreads,completions_seen,branch_count);
`ifndef BPRED_DISABLED
    assert(predicted_conditional>0) else $fatal(1,"warm conditional branch never used the BTB");
`else
    assert(predicted_branches==0) else $fatal(1,"disabled predictor emitted a prediction");
`endif
    $display("RV2Wide initial fetch phase: cycles=%0d corrections=%0d predictions=%0d",cycles,branch_count,predicted_branches);
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
      int pc;
      pc='h414+width*16;
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
      logic [63:0] pte;
      pte=('h2<<10)|'hc7;
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
    parcel('h638,c_imm(0,31,-1)); insn('h63a,addi(0,0,0));
    insn('h63e,bne(31,0,'h608-'h63e)); // warm predicted branch crosses the eight-byte boundary
    insn('h642,addi(20,20,1));
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
`ifndef BPRED_DISABLED
    assert(predicted_straddles>0) else $fatal(1,"warm straddling branch never used the BTB");
`endif
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
        logic [63:0] pte;
        logic [31:0] crossing;
        pte=scenario==17 ? ('h16<<10)|'hcb : 0;
        crossing=addi(12,0,77);
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
    // Fetch mixed compressed sources and full-width M operations through the
    // production caches; WFI waits for every accepted deferred write.
    @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0;
    for(int r=0;r<32;r++) registers[r]=0;
    parcel('h600,c_imm(2,8,-17)); parcel('h602,c_imm(2,9,7));
    insn('h604,{7'd1,5'd9,5'd8,3'd0,5'd10,7'h33}); parcel('h608,c_imm(2,11,3));
    insn('h60a,{7'd1,5'd9,5'd10,3'd4,5'd12,7'h33}); parcel('h60e,16'h86b2);
    insn('h610,{7'd1,5'd9,5'd8,3'd0,5'd14,7'h3b});
    insn('h614,{7'd1,5'd9,5'd10,3'd5,5'd15,7'h33}); insn('h618,32'h10500073);
    phase=20; reference_pc='h600;
    repeat(3) @(negedge clock); reset=0;
    @(negedge clock); start_in='{valid:1'b1,bits:64'h600};
    @(negedge clock); start_in='0;
    wait(sleeping); repeat(3) @(negedge clock);
    assert(registers[10]==-64'd119 && registers[12]==-64'd17 && registers[13]==-64'd17 && registers[14]==-64'd119 && completions.size()==0 && reference_pc=='h61c) else $fatal(1,"fetching M service/drain");
    // B results feed the production LSU and compressed consumers without an
    // extra execution stage; retain all bits through store/load forwarding.
    @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0;
    for(int r=0;r<32;r++) registers[r]=0;
    parcel('h700,c_imm(2,8,17)); parcel('h702,c_imm(2,9,3));
    insn('h704,{7'h10,5'd9,5'd8,3'd4,5'd10,7'h33}); // SH2ADD
    insn('h708,{6'h0a,6'd63,5'd10,3'd1,5'd11,7'h13}); // BSETI
    insn('h70c,{12'h6b8,5'd11,3'd5,5'd12,7'h13}); // REV8
    insn('h710,addi(1,0,'h400)); insn('h714,store(12,1,0,3)); insn('h718,load(13,1,0,3));
    parcel('h71c,16'h8736); insn('h71e,32'h10500073); // C.MV x14,x13; WFI
    phase=21; reference_pc='h700;
    repeat(3) @(negedge clock); reset=0;
    @(negedge clock); start_in='{valid:1'b1,bits:64'h700};
    @(negedge clock); start_in='0;
    wait(sleeping); repeat(3) @(negedge clock);
    assert(registers[10]==71 && registers[11]==64'h8000000000000047 && registers[12]==64'h4700000000000080 && registers[13]==registers[12] && registers[14]==registers[12] && completions.size()==0 && reference_pc=='h722)
      else $fatal(1,"fetching B/LSU/forwarding");
    // Full fetch/MMU/cache composition: old-value W sign extension, SC status,
    // accepted x0 effects, and branch-killed SC with no mutation.
    @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0; reservation_valid=0;
    for(int r=0;r<32;r++) registers[r]=0;
    for(int b=0;b<8;b++) begin backing['h340+b]=8'hff; model_bytes['h340+b]=8'hff; end
    insn('h700,addi(1,0,'h340)); insn('h704,addi(2,0,7));
    insn('h708,atomic_insn(2,2,3,1)); insn('h70c,addi(10,0,10));
    insn('h710,atomic_insn(0,2,4,1,2)); insn('h714,atomic_insn(3,2,5,1,2));
    insn('h718,atomic_insn(2,3,6,1)); insn('h71c,atomic_insn(3,3,7,1,2));
    insn('h720,addi(8,7,1)); insn('h724,atomic_insn(1,2,0,1,0));
    insn('h728,load(9,1,0,3)); insn('h72c,atomic_insn(2,3,0,1));
    insn('h730,jal(0,12)); insn('h734,atomic_insn(3,3,0,1,2)); insn('h738,addi(11,0,99));
    insn('h73c,load(12,1,0,3)); insn('h740,32'h10500073);
    phase=22; reference_pc='h700;
    repeat(3) @(negedge clock); reset=0;
    @(negedge clock); start_in='{valid:1'b1,bits:64'h700};
    @(negedge clock); start_in='0;
    wait(sleeping); repeat(3) @(negedge clock);
    assert(registers[3]=='1 && registers[4]=='1 && registers[5]==1 && registers[6]==64'hffffffff00000006 && registers[7]==0 && registers[8]==1 && registers[9]==0 && registers[10]==10 && registers[11]==0 && registers[12]==0 && completions.size()==0 && reference_pc=='h744)
      else $fatal(1,"fetching atomic values, ownership, or branch cancellation");
    // Independently translated fragments: the second virtual page deliberately
    // maps to a nonadjacent PA. Missing mappings retain the exact fault VA and
    // leave an accepted store prefix visible to the real trap handler.
    for(int scenario=23;scenario<=25;scenario++) begin
      logic [63:0] pte, prefix;
      @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0; ustate=0;
      for(int r=0;r<32;r++) registers[r]=0;
      for(int b='h10000;b<'h20000;b++) begin backing[b]=0; model_bytes[b]=0; end
      pte=('h11<<10)|1; for(int b=0;b<8;b++) backing['h10000+b]=pte[b*8+:8];
      pte=('h12<<10)|1; for(int b=0;b<8;b++) backing['h11010+b]=pte[b*8+:8];
      pte=('h14<<10)|'hcb; for(int b=0;b<8;b++) backing['h12000+b]=pte[b*8+:8];
      pte=('h15<<10)|'hc7; for(int b=0;b<8;b++) backing['h12800+b]=pte[b*8+:8];
      pte=scenario==23 ? ('h17<<10)|'hc7 : 0;
      for(int b=0;b<8;b++) backing['h12808+b]=pte[b*8+:8];
      for(int b=0;b<16;b++) begin
        backing['h15ff0+b]=8'('h80+b); model_bytes['h15ff0+b]=backing['h15ff0+b];
        backing['h17000+b]=8'('h90+b); model_bytes['h17000+b]=backing['h17000+b];
      end
      insn('h300,addi(1,0,'h380)); insn('h304,{12'h305,5'd1,3'b001,5'd0,7'h73});
      insn('h308,addi(1,0,8)); insn('h30c,{6'd0,6'd60,5'd1,3'b001,5'd1,7'h13});
      insn('h310,addi(1,1,16)); insn('h314,{12'h180,5'd1,3'b001,5'd0,7'h73});
      insn('h318,32'h12000073); insn('h31c,addi(1,0,2047)); insn('h320,addi(1,1,1));
      insn('h324,{12'h300,5'd1,3'b001,5'd0,7'h73});
      insn('h328,{20'h400,5'd1,7'h37}); insn('h32c,{12'h341,5'd1,3'b001,5'd0,7'h73}); insn('h330,32'h30200073);
      insn('h14000,{20'h501,5'd1,7'h37}); insn('h14004,addi(1,1,-3)); insn('h14008,addi(2,0,'h123));
      insn('h1400c,scenario==25 ? store(2,1,0,3) : load(3,1,0,3));
      if(scenario==23) begin
        insn('h14010,load(4,1,0,3)); insn('h14014,addi(5,4,1));
        insn('h14018,store(2,1,0,3)); insn('h1401c,load(6,1,0,3));
        insn('h14020,load(0,1,0,3)); insn('h14024,32'h10500073);
      end else insn('h14010,addi(4,0,99));
      insn('h380,{12'h342,5'd0,3'b010,5'd10,7'h73});
      insn('h384,{12'h343,5'd0,3'b010,5'd11,7'h73});
      insn('h388,{12'h341,5'd0,3'b010,5'd13,7'h73});
      insn('h38c,{20'h16,5'd1,7'h37}); insn('h390,addi(1,1,-8));
      insn('h394,load(14,1,0,3)); insn('h398,32'h10500073);
      phase=scenario; reference_pc='h300; expected_fault_pc='h40000c;
      expected_fault_cause=scenario==25 ? 15 : 13; expected_fault_value='h501000;
      repeat(3) @(negedge clock); reset=0;
      @(negedge clock); start_in='{valid:1'b1,bits:64'h300};
      @(negedge clock); start_in='0;
      wait(sleeping); repeat(3) @(negedge clock);
      if(scenario==23) assert(registers[3]==64'h94939291908f8e8d && registers[4]==registers[3] && registers[5]==registers[3]+1 && registers[6]=='h123 && reference_pc=='h400028)
        else $fatal(1,"noncontiguous translated split completion");
      else begin
        prefix=scenario==25 ? 64'h0001238c8b8a8988 : 64'h8f8e8d8c8b8a8988;
        assert(registers[3]==0 && registers[4]==0 && registers[10]==64'(expected_fault_cause) && registers[11]=='h501000 && registers[13]=='h40000c && registers[14]==prefix)
          else $fatal(1,"second-page trap provenance, younger squash, or partial store prefix");
      end
      assert(completions.size()==0) else $fatal(1,"split allocated a deferred RF owner");
    end
`ifndef BPRED_DISABLED
    // All Zcb forms execute through the normal decoder, including every compact
    // register, dependent M/B results, cold narrow loads, and masked stores.
    begin
      int pc;
      @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0;
      for(int r=0;r<32;r++) registers[r]=0;
      for(int b='he00;b<'he20;b++) begin backing[b]=8'('h80+b); model_bytes[b]=backing[b]; end
      pc='h600;
      for(int rd=8;rd<16;rd++) for(int op='h18;op<='h1d;op++) begin
        insn(pc,addi(rd,0,-129)); pc+=4;
        parcel(pc,c_zcb_unary(rd,op)); pc+=2;
        insn(pc,addi(17,rd,1)); pc+=4;
      end
      for(int rd=8;rd<16;rd++) begin
        int rs;
        rs=8+((rd+1)&7);
        insn(pc,addi(rd,0,-7)); pc+=4;
        insn(pc,addi(rs,0,3)); pc+=4;
        parcel(pc,{6'h27,3'(rd-8),2'b10,3'(rs-8),2'b01}); pc+=2;
        insn(pc,addi(17,rd,1)); pc+=4;
      end
      insn(pc,{20'd1,5'd8,7'h37}); pc+=4;
      insn(pc,addi(8,8,-512)); pc+=4;
      for(int offset=0;offset<4;offset++) begin
        parcel(pc,c_zcb_memory('h20,9+offset,8,offset)); pc+=2;
        insn(pc,addi(9+offset,9+offset,1)); pc+=4;
        parcel(pc,c_zcb_memory('h22,9+offset,8,offset)); pc+=2;
        parcel(pc,c_zcb_memory('h20,13,8,offset)); pc+=2;
      end
      for(int offset=0;offset<4;offset+=2) begin
        parcel(pc,c_zcb_memory('h21,10,8,offset)); pc+=2; // LHU
        parcel(pc,c_zcb_memory('h21,11,8,offset,1)); pc+=2; // LH
        parcel(pc,c_zcb_memory('h23,11,8,offset)); pc+=2; // SH
        parcel(pc,c_zcb_memory('h21,12,8,offset)); pc+=2;
      end
      // C.MOP preserves its encoded register, including x1, and has no operands.
      for(int index=1;index<16;index+=2) begin
        insn(pc,addi(index,0,50+index)); pc+=4;
        parcel(pc,{3'b011,1'b0,5'(index),5'd0,2'b01}); pc+=2;
        insn(pc,addi(16,index,0)); pc+=4;
      end
      insn(pc,jal(0,6)); pc+=4;
      parcel(pc,c_zcb_memory('h22,9,8,0)); pc+=2; // wrong-path mutation
      parcel(pc,c_zcb_memory('h20,15,8,0)); pc+=2;
      insn(pc,32'h10500073); pc+=4;
      phase=26; reference_pc='h600;
      repeat(3) @(negedge clock); reset=0;
      @(negedge clock); start_in='{valid:1'b1,bits:64'h600};
      @(negedge clock); start_in='0;
      wait(sleeping); repeat(3) @(negedge clock);
      assert(reference_pc==pc && zc_pairs>0 && registers[16]==65 && registers[15]=='h81 && completions.size()==0)
        else $fatal(1,"Zcb/Zcmop execution, pairing, dependencies, or drain");
    end
`endif
    // Selected-subset legality must preserve raw 16-bit trap values and drain
    // an older accepted load. The C-only variant rejects these optional forms;
    // the extended variant rejects adjacent reserved encodings instead.
    for(int scenario=0;scenario<2;scenario++) begin
      logic [15:0] invalid;
`ifdef BPRED_DISABLED
      invalid=scenario==0 ? 16'h9c75 : 16'h6081; // C.NOT / C.MOP.1 disabled
`else
      invalid=scenario==0 ? 16'h9c79 : 16'h6101; // reserved unary / zero C.ADDI16SP
`endif
      @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0;
      for(int r=0;r<32;r++) registers[r]=0;
      for(int b=0;b<8;b++) begin backing['h800+b]=8'('ha0+b); model_bytes['h800+b]=backing['h800+b]; end
      insn('h380,{12'h342,5'd0,3'b010,5'd10,7'h73});
      insn('h384,{12'h343,5'd0,3'b010,5'd11,7'h73});
      insn('h388,{12'h341,5'd0,3'b010,5'd13,7'h73}); insn('h38c,32'h10500073);
      insn('h400,addi(1,0,'h380)); insn('h404,{12'h305,5'd1,3'b001,5'd0,7'h73});
      insn('h408,addi(8,0,2047)); insn('h40c,addi(8,8,1));
      parcel('h410,16'h6004); parcel('h412,invalid); parcel('h414,c_imm(2,12,9));
      expected_fault_pc='h412; expected_fault_cause=2; expected_fault_value=64'(invalid);
      phase=27; reference_pc='h400;
      repeat(3) @(negedge clock); reset=0;
      @(negedge clock); start_in='{valid:1'b1,bits:64'h400};
      @(negedge clock); start_in='0;
      wait(sleeping); repeat(3) @(negedge clock);
      assert(registers[9]==64'ha7a6a5a4a3a2a1a0 && registers[11]==64'(invalid) && registers[13]=='h412 && registers[12]==0)
        else $fatal(1,"compressed subset legality/trap provenance/drain");
    end
`ifndef BPRED_DISABLED
    // Repeated best-effort I hints in a resident loop eventually fill a cold
    // target. The later branch uses that line without another CHI request.
    @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0;
    for(int r=0;r<32;r++) registers[r]=0;
    insn('h600,32'h000010b7); insn('h604,addi(1,1,-1024));
    insn('h608,addi(2,0,40));
    insn('h60c,{7'd0,5'd0,5'd1,3'b110,5'd0,7'h13});
    insn('h610,addi(2,2,-1)); insn('h614,bne(2,0,-8));
    insn('h618,jal(0,'hc00-'h618));
    insn('hc00,addi(5,0,123)); insn('hc04,32'h10500073);
    phase=28; reference_pc='h600;
    repeat(3) @(negedge clock); reset=0;
    @(negedge clock); start_in='{valid:1'b1,bits:64'h600};
    @(negedge clock); start_in='0;
    wait(sleeping); repeat(3) @(negedge clock);
    assert(instruction_prefetch_reads==1 && registers[5]==123)
      else $fatal(1,"instruction prefetch traffic/benefit reads=%0d",instruction_prefetch_reads);
    // A timed wait keeps its LR reservation: SC still succeeds after the
    // feed-forward pipeline has been flushed and its retained owner retires.
    @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0;
    for(int r=0;r<32;r++) registers[r]=0;
    insn('h600,addi(1,0,2047)); insn('h604,addi(1,1,1));
    insn('h608,addi(2,0,7)); insn('h60c,atomic_insn(2,3,3,1));
    insn('h610,32'h01d00073); insn('h614,atomic_insn(3,3,4,1,2));
    insn('h618,32'h10500073);
    for(int b=0;b<8;b++) begin backing['h800+b]=8'(b); model_bytes['h800+b]=8'(b); end
    phase=29; reference_pc='h600;
    repeat(3) @(negedge clock); reset=0;
    @(negedge clock); start_in='{valid:1'b1,bits:64'h600};
    @(negedge clock); start_in='0;
    wait(sleeping); repeat(32) @(negedge clock);
    assert(reference_pc=='h610 && sleeping) else $fatal(1,"STO did not retain its instruction");
    wait(reference_pc=='h61c && sleeping); repeat(3) @(negedge clock);
    assert(registers[4]==0 && model_bytes['h800]==7) else $fatal(1,"STO destroyed the reservation");
    // Hints flow through real fetch/decode and the same traced retirement
    // stream; instruction buffering survives each bounded issue cooldown.
    @(negedge clock); reset=1; iactive=0; dactive=0; wactive=0;
    for(int r=0;r<32;r++) registers[r]=0;
    insn('h600,addi(1,0,7)); insn('h604,32'h0100000f);
    insn('h608,addi(2,1,1)); insn('h60c,32'h0100000f);
    insn('h610,addi(3,2,1)); insn('h614,32'h10500073);
    phase=30; reference_pc='h600;
    repeat(3) @(negedge clock); reset=0;
    @(negedge clock); start_in='{valid:1'b1,bits:64'h600};
    @(negedge clock); start_in='0;
    wait(sleeping); repeat(3) @(negedge clock);
    assert(pauses==2 && registers[3]==9 && cycles-last_pause_cycle>=16)
      else $fatal(1,"PAUSE fetch/retirement/cooldown integration");
`endif
    $display("RV2Wide fetching core passed: %0d retirements, %0d-cycle dual run, %0d I refills, %0d D refills, %0d faults, %0d IO reads/%0d writes, %0d fences",commits,longest_dual,ireads,dreads,faults,ureads,uwrites,fences);
`ifndef BPRED_DISABLED
    rv2wide_fetch_trace_finish();
`endif
    $finish;
  end
endmodule
