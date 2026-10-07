// Checks RV2Wide execution against the production data cache and independent CHI memory oracle.
// SPDX-License-Identifier: Apache-2.0
module rv2wide_cache_tb;
  typedef struct packed { logic [63:0] cause, value; } fetch_fault_t;
  typedef struct packed { logic valid; fetch_fault_t bits; } fetch_fault_flow_t;
  typedef struct packed { logic valid; logic [63:0] pc, target; logic compressed; logic [1:0] ras_action; } prediction_t;
  typedef struct packed { logic [63:0] pc; logic [31:0] instruction, raw_instruction; logic [63:0] sequential_pc; logic compressed_illegal; fetch_fault_flow_t fault; prediction_t prediction; logic [1:0] speculated_ras_action; } instruction_t;
  typedef struct packed { logic [1:0] count; instruction_t [1:0] entries; } packet_t;
  typedef struct packed { logic valid; packet_t bits; } packet_flow_t;
  typedef struct packed { logic [1:0] disposition; logic [63:0] cause, value; } resolution_t;
  typedef struct packed { instruction_t fetched; logic [4:0] rd; logic write; logic [63:0] data; logic deferred; } retirement_t;
  typedef struct packed { logic valid; retirement_t bits; } retirement_flow_t;
  typedef struct packed { logic [63:0] pc, target; resolution_t resolution; } redirect_t;
  typedef struct packed { logic valid; redirect_t bits; } redirect_flow_t;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_t;
  typedef struct packed { logic valid; CHISnpFlit bits; } snp_t;
  typedef struct packed { ready_t requests, requester_responses, request_data; rsp_t responses; dat_t response_data; snp_t snoops; } chi_in_t;
  typedef struct packed { req_t requests; rsp_t requester_responses; dat_t request_data; ready_t responses, response_data, snoops; } chi_out_t;
  logic clock = 0, reset = 1;
  packet_flow_t instructions;
  logic instructions_ready;
  retirement_flow_t retired[2], completed;
  redirect_flow_t redirect;
  logic [1:0] issued, retired_count;
  wire uncached_activity;
  chi_in_t chi_in;
  chi_out_t chi_out;
  RV2WideCacheFixture dut(.clock(clock), .reset(reset), .node_id(7'd3),
    .instructions_in(instructions), .instructions_out(instructions_ready),
    .retired_0_out(retired[0]), .retired_1_out(retired[1]), .completed_out(completed),
    .redirect_out(redirect), .issued(issued), .retired_count(retired_count), .chi_in(chi_in), .chi_out(chi_out),
    .uncached_chi_in('0), .uncached_chi_out(), .uncached_activity(uncached_activity));
  always #5 clock = ~clock;
  logic [31:0] program_words[1024];
  byte unsigned backing[4096], reference_bytes[4096];
  logic [63:0] registers[32];
  retirement_t completion_queue[$];
  int program_size, send_pc, reference_pc, cycles, commits, dual_commits, replays, branches;
  int reads, writes, completions, hits_during_miss, alu_during_miss;
  bit fault_seen, read_active, write_active;
  CHIReqFlit pending_read, pending_write;
  int read_due, read_packet;
  bit reservation_valid=0, probe_pending=0, probe_accepted=0, probe_complete=0;
  logic [63:0] reservation_address;
  int reservation_width, probe_lr_pc, probe_sc_pc, atomic_commits=0, atomic_dual=0, sc_success=0, sc_failure=0;
  int atomic_ops[9]='{1,0,4,12,8,16,20,24,28};
  int split_resumes=0;
  int zero_commits=0;
  bit maintenance_active=0, maintenance_snooped=0, maintenance_data=0;
  CHIReqFlit pending_maintenance;
  int maintenance_due, maintenance_requests=0, maintenance_responses=0, maintenance_commits=0;
  int after_maintenance_reads, after_maintenance_misses;
  int clean_resident_pc=-1;
  bit check_maintenance_hit=0;
  bit check_maintenance_completion=0;
  logic [63:0] maintenance_load_pc;

  function automatic logic [31:0] addi(int rd, rs1, imm);
    return {12'(imm), 5'(rs1), 3'b000, 5'(rd), 7'h13};
  endfunction
  function automatic logic [31:0] load(int rd, rs1, imm, width);
    return {12'(imm), 5'(rs1), 3'(width), 5'(rd), 7'h03};
  endfunction
  function automatic logic [31:0] store_insn(int rs2, rs1, imm, width);
    return {7'(imm >> 5), 5'(rs2), 5'(rs1), 3'(width), 5'(imm), 7'h23};
  endfunction
  function automatic logic [31:0] jal(int rd, imm);
    return {1'(imm >> 20), 10'(imm >> 1), 1'(imm >> 11), 8'(imm >> 12), 5'(rd), 7'h6f};
  endfunction
  function automatic logic [31:0] atomic_insn(int op, width, rd, rs1, rs2, order_bits=3);
    return {5'(op),2'(order_bits),5'(rs2),5'(rs1),3'(width),5'(rd),7'h2f};
  endfunction
  task automatic emit(logic [31:0] instruction);
    program_words[program_size++] = instruction;
  endtask

  // A byte-addressed backing memory is independent of cache tags, ownership, and replacement.
  always_comb begin
    chi_in = '0;
    chi_in.requests.ready = !read_active && !write_active && !maintenance_active && cycles % 5 != 0;
    chi_in.requester_responses.ready = cycles % 4 != 0;
    chi_in.request_data.ready = cycles % 3 != 0;
    chi_in.snoops.valid = probe_pending && !probe_accepted;
    chi_in.snoops.bits.opcode = 5'h09;
    chi_in.snoops.bits.address = 41'(768 >> 3);
    chi_in.snoops.bits.src_id = 7'd1;
    chi_in.snoops.bits.txn_id = 12'h100;
    if(maintenance_active && !maintenance_snooped) begin
      chi_in.snoops.valid=1;
      chi_in.snoops.bits.opcode=5'(pending_maintenance.opcode);
      chi_in.snoops.bits.address=41'(pending_maintenance.address>>3);
      chi_in.snoops.bits.txn_id=12'h101;
    end
    if (write_active) begin
      chi_in.responses.valid = 1;
      chi_in.responses.bits.opcode = 5'h05;
      chi_in.responses.bits.src_id = 7'd1;
      chi_in.responses.bits.tgt_id = 7'd3;
      chi_in.responses.bits.txn_id = pending_write.txn_id;
      chi_in.responses.bits.dbid_or_group_id = 12'd9;
    end
    if(maintenance_active && maintenance_data && cycles>=maintenance_due) begin
      chi_in.responses.valid=1;
      chi_in.responses.bits.opcode=5'h04;
      chi_in.responses.bits.src_id=7'd1;
      chi_in.responses.bits.tgt_id=7'd3;
      chi_in.responses.bits.txn_id=pending_maintenance.txn_id;
    end
    if (read_active && cycles >= read_due) begin
      chi_in.response_data.valid = 1;
      chi_in.response_data.bits.opcode = 4'h4;
      chi_in.response_data.bits.src_id = 7'd1;
      chi_in.response_data.bits.tgt_id = 7'd3;
      chi_in.response_data.bits.home_nid_or_pbha_or_mismatched_mecid = 7'd1;
      chi_in.response_data.bits.txn_id = pending_read.txn_id;
      chi_in.response_data.bits.dbid_or_mecid = 16'd5;
      chi_in.response_data.bits.resp = pending_read.opcode == 7'h07 ? 3'd2 : 3'd1;
      chi_in.response_data.bits.data_id = 2'(read_packet);
      chi_in.response_data.bits.byte_enable = '1;
      for (int b = 0; b < 16; b++)
        chi_in.response_data.bits.data[b*8 +: 8] = backing[int'(pending_read.address) + 16*read_packet + b];
    end
  end

  task automatic check_retirement(retirement_t got);
    logic [31:0] insn;
    logic [63:0] value, address;
    logic signed [63:0] imm;
    int rd, rs1, rs2, width, bytes_count;
    bit writes_rd;
    retirement_t expected;
    assert (got.fetched.pc == 64'(reference_pc)) else $fatal(1, "retirement PC got=%h expected=%h", got.fetched.pc, reference_pc);
    insn = program_words[reference_pc/4];
    assert (got.fetched.instruction == insn) else $fatal(1, "instruction mismatch");
    rd = int'(insn[11:7]); rs1 = int'(insn[19:15]); rs2 = int'(insn[24:20]); width = int'(insn[14:12]);
    value = 0; writes_rd = 0; reference_pc += 4;
    case (insn[6:0])
      7'h0f: begin
        assert(insn[31:20] inside {0,1,2,4} && width==2 && rd==0) else $fatal(1,"unexpected CBO");
        address=registers[rs1]&~64'd63;
        if(insn[31:20]==4) begin
          for(int b=0;b<64;b++) reference_bytes[int'(address)+b]=0;
          if(reservation_valid && address/64==reservation_address/64) reservation_valid=0;
          assert(got.deferred && !got.write) else $fatal(1,"CBO bypassed WB-owned service");
          zero_commits++;
        end else begin
          assert(!got.deferred && !got.write && !maintenance_active) else $fatal(1,"maintenance retired before CHI completion");
          if(address<4096) begin
            assert(maintenance_responses==maintenance_commits+1) else $fatal(1,"maintenance response ownership");
            for(int b=0;b<64;b++) assert(backing[int'(address)+b]==reference_bytes[int'(address)+b])
              else $fatal(1,"maintenance did not publish dirty bytes address=%h",address+64'(b));
            after_maintenance_reads=reads;
            after_maintenance_misses=got.fetched.pc==64'(clean_resident_pc) ? 0 : 1;
            check_maintenance_hit=1;
          end
          maintenance_commits++;
        end
      end
      7'h13: begin value = registers[rs1] + 64'($signed(insn[31:20])); writes_rd = rd != 0; end
      7'h37: begin value={{32{insn[31]}},insn[31:12],12'b0}; writes_rd=rd!=0; end
      7'h03: begin
        address = registers[rs1] + 64'($signed(insn[31:20]));
        bytes_count = 1 << (width & 3);
        for (int b = 0; b < bytes_count; b++) value[b*8 +: 8] = reference_bytes[int'(address)+b];
        if (width < 4 && bytes_count < 8 && value[bytes_count*8-1]) value |= ~64'd0 << (bytes_count*8);
        writes_rd = rd != 0;
        if (read_active && !got.deferred) hits_during_miss++;
        if(check_maintenance_hit) begin
          if(got.deferred) begin
            check_maintenance_completion=1;
            maintenance_load_pc=got.fetched.pc;
          end else assert(reads==after_maintenance_reads+after_maintenance_misses) else $fatal(1,"unexpected maintenance refill count");
          check_maintenance_hit=0;
        end
      end
      7'h23: begin
        imm = 64'($signed({insn[31:25],insn[11:7]})); address = registers[rs1] + imm;
        for (int b = 0; b < (1 << width); b++) reference_bytes[int'(address)+b] = registers[rs2][b*8 +: 8];
        if(reservation_valid && address/64==reservation_address/64) reservation_valid=0;
      end
      7'h2f: begin
        logic [63:0] replacement, operand;
        bit successful;
        address=registers[rs1]; bytes_count=1<<width;
        for(int b=0;b<bytes_count;b++) value[b*8+:8]=reference_bytes[int'(address)+b];
        if(bytes_count==4) value={{32{value[31]}},value[31:0]};
        operand=bytes_count==4 ? {{32{registers[rs2][31]}},registers[rs2][31:0]} : registers[rs2];
        replacement=value;
        case(insn[31:27])
          2: begin reservation_valid=1; reservation_address=address; reservation_width=width; end
          3: begin
            successful=reservation_valid && reservation_address==address && reservation_width==width;
            value=successful ? 0 : 1; replacement=operand; reservation_valid=0;
            if(successful) sc_success++; else sc_failure++;
          end
          1: replacement=operand;
          0: replacement=value+operand;
          4: replacement=value^operand;
          12: replacement=value&operand;
          8: replacement=value|operand;
          16: replacement=$signed(value)<$signed(operand) ? value : operand;
          20: replacement=$signed(value)>$signed(operand) ? value : operand;
          24: replacement=value<operand ? value : operand;
          28: replacement=value>operand ? value : operand;
          default: $fatal(1,"unknown AMO");
        endcase
        if(insn[31:27]!=2 && (insn[31:27]!=3 || successful)) begin
          for(int b=0;b<bytes_count;b++) reference_bytes[int'(address)+b]=replacement[b*8+:8];
          if(insn[31:27]!=3 && reservation_valid && address/64==reservation_address/64) reservation_valid=0;
        end
        writes_rd=rd!=0; atomic_commits++;
        assert(got.deferred) else $fatal(1,"atomic did not use WB-authorized service");
      end
      7'h6f: begin
        value = 64'(reference_pc); writes_rd = rd != 0;
        imm = 64'($signed({insn[31],insn[19:12],insn[20],insn[30:21],1'b0}));
        reference_pc = int'(got.fetched.pc + imm);
      end
      default: $fatal(1, "unmodeled instruction %h", insn);
    endcase
    if (read_active && insn[6:0] == 7'h13) alu_during_miss++;
    assert (got.write == writes_rd) else $fatal(1, "write flag at %h", got.fetched.pc);
    if (writes_rd) begin
      assert (got.rd == 5'(rd)) else $fatal(1, "destination at %h", got.fetched.pc);
      if (!got.deferred) assert (got.data == value) else $fatal(1, "value at %h got %h expected %h", got.fetched.pc, got.data, value);
      registers[rd] = value;
    end
    if (got.deferred) begin
      expected = got; expected.data = value;
      completion_queue.push_back(expected);
    end
    commits++;
  endtask

  always @(negedge clock) begin
    instructions = '0;
    if (!reset && !fault_seen && send_pc/4 < program_size && (send_pc<probe_sc_pc || probe_complete)) begin
      instructions.valid = 1;
      instructions.bits.count = send_pc/4+1 < program_size ? 2 : 1;
      if(send_pc+4==probe_sc_pc && !probe_complete) instructions.bits.count=1;
      instructions.bits.entries[0] = '{64'(send_pc), program_words[send_pc/4], program_words[send_pc/4], 64'(send_pc+4), 1'b0, '0, '0, 2'd0};
      instructions.bits.entries[1] = '{64'(send_pc+4), program_words[send_pc/4+1], program_words[send_pc/4+1], 64'(send_pc+8), 1'b0, '0, '0, 2'd0};
    end
  end
  always @(posedge clock) if (!reset) begin
    assert(!uncached_activity) else $fatal(1,"uncached maintenance emitted device IO");
    cycles <= cycles + 1;
    if (cycles > 10000) $fatal(1, "cache/core timeout pc=%h reference=%h reads=%0d completions=%0d atomic=%0d probe=%b/%b/%b LR=%h SC=%h", send_pc, reference_pc, reads, completions,atomic_commits,probe_pending,probe_accepted,probe_complete,probe_lr_pc,probe_sc_pc);
    if (instructions.valid && instructions_ready) send_pc += 4*int'(instructions.bits.count);
    if (retired[0].valid && retired[1].valid) dual_commits++;
    if(retired[0].valid && retired[1].valid && (retired[0].bits.fetched.instruction[6:0]==7'h2f || retired[1].bits.fetched.instruction[6:0]==7'h2f)) atomic_dual++;
    for (int slot = 0; slot < 2; slot++) if (retired[slot].valid) check_retirement(retired[slot].bits);
    if (completed.valid) begin
      retirement_t expected;
      assert (completion_queue.size() > 0) else $fatal(1, "unexpected completion");
      expected = completion_queue.pop_front();
      assert (completed.bits.fetched == expected.fetched && completed.bits.rd == expected.rd && completed.bits.write == expected.write)
        else $fatal(1, "completion owner mismatch");
      if (expected.write) assert (completed.bits.data == expected.data) else $fatal(1, "load completion got=%h expected=%h at %h", completed.bits.data, expected.data, expected.fetched.pc);
      if(check_maintenance_completion && completed.bits.fetched.pc==maintenance_load_pc) begin
        assert(reads==after_maintenance_reads+after_maintenance_misses) else $fatal(1,"unexpected maintenance refill count");
        check_maintenance_completion=0;
      end
      completions++;
      if(completed.bits.fetched.pc==64'(probe_lr_pc)) probe_pending<=1;
    end
    if (redirect.valid) begin
      case (redirect.bits.resolution.disposition)
        0: begin send_pc = int'(redirect.bits.target); branches++; end
        2: begin send_pc = int'(redirect.bits.pc); replays++; end
        3: begin
          assert(redirect.bits.target==64'(reference_pc)) else $fatal(1,"split resume did not follow retained retirement");
          send_pc=int'(redirect.bits.target); split_resumes++;
        end
        1: begin
          assert (redirect.bits.pc == 64'(reference_pc) && redirect.bits.resolution.cause == 5 && redirect.bits.resolution.value == 4096)
            else $fatal(1, "bad fault pc=%h cause=%h value=%h", redirect.bits.pc, redirect.bits.resolution.cause, redirect.bits.resolution.value);
          assert (completion_queue.size() == 0) else $fatal(1, "fault before committed completion drain");
          fault_seen = 1;
        end
        default: $fatal(1, "bad disposition");
      endcase
    end
    if (chi_out.requests.valid && chi_in.requests.ready) begin
      if ($test$plusargs("debug")) $display("%0d REQ op=%h addr=%h", cycles, chi_out.requests.bits.opcode, chi_out.requests.bits.address);
      assert (chi_out.requests.bits.address < 4096) else $fatal(1, "unmapped address reached CHI");
      case (chi_out.requests.bits.opcode)
        7'h02, 7'h07: begin
          pending_read <= chi_out.requests.bits;
          read_due <= cycles + 35; read_packet <= 0; read_active <= 1; reads++;
        end
        7'h1b: begin pending_write <= chi_out.requests.bits; write_active <= 1; writes++; end
        7'h08, 7'h09: begin
          assert(chi_out.requests.bits.excl_snoop_me_cah && chi_out.requests.bits.address[5:0]==0)
            else $fatal(1,"maintenance omitted requester snoop or block alignment");
          pending_maintenance<=chi_out.requests.bits; maintenance_active<=1;
          maintenance_snooped<=0; maintenance_data<=0; maintenance_due<=cycles+40;
          maintenance_requests++;
        end
        default: $fatal(1, "unexpected request opcode %h", chi_out.requests.bits.opcode);
      endcase
    end
    if (chi_in.response_data.valid && chi_out.response_data.ready) begin
      if ($test$plusargs("debug")) $display("%0d DAT packet=%0d", cycles, read_packet);
      if (read_packet == 3) read_active <= 0;
      else begin read_packet <= read_packet + 1; read_due <= cycles + 2; end
    end
    if(chi_in.snoops.valid && chi_out.snoops.ready) begin
      if(chi_in.snoops.bits.txn_id==12'h101) maintenance_snooped<=1;
      else probe_accepted<=1;
      reservation_valid=0;
      if($test$plusargs("debug")) $display("%0d PROBE",cycles);
    end
    if(chi_out.requester_responses.valid && chi_in.requester_responses.ready) begin
      if($test$plusargs("debug")) $display("%0d ACK",cycles);
      if(chi_out.requester_responses.bits.opcode==5'h01 && chi_out.requester_responses.bits.txn_id==12'h100) probe_complete<=1;
      if(chi_out.requester_responses.bits.opcode==5'h01 && chi_out.requester_responses.bits.txn_id==12'h101) maintenance_data<=1;
    end
    if (chi_in.responses.valid && chi_out.responses.ready) begin
      if(maintenance_active) begin maintenance_active<=0; maintenance_responses++; end
      else write_active<=0;
    end
    if (chi_out.request_data.valid && chi_in.request_data.ready) begin
      if($test$plusargs("debug")) $display("%0d WRITE DATA opcode=%h dataID=%h",cycles,chi_out.request_data.bits.opcode,chi_out.request_data.bits.data_id);
      assert (chi_out.request_data.bits.opcode inside {4'h1,4'h2}) else $fatal(1, "unexpected write data");
      for (int b = 0; b < 16; b++) if (chi_out.request_data.bits.byte_enable[b])
        backing[(chi_out.request_data.bits.opcode==1 ? (maintenance_active ? int'(pending_maintenance.address) : 768) : int'(pending_write.address))+16*int'(chi_out.request_data.bits.data_id)+b] = chi_out.request_data.bits.data[b*8 +: 8];
      if(chi_out.request_data.bits.opcode==1 && chi_out.request_data.bits.data_id==3) begin
        if(maintenance_active) maintenance_data<=1;
        else probe_complete<=1;
      end
    end
  end

  initial begin
    program_size = 0; send_pc = 0; reference_pc = 0; cycles = 0;
    for (int i = 0; i < 4096; i++) begin backing[i] = 8'(i); reference_bytes[i] = 8'(i); end
    for (int i = 0; i < 32; i++) registers[i] = 0;
    emit(addi(1,0,0)); emit(addi(2,0,85));
    emit(store_insn(2,1,24,3)); emit(addi(4,0,7));
    emit(load(3,1,24,3)); emit(addi(5,4,2));
    emit(addi(6,3,1)); emit(store_insn(6,1,25,0));
    emit(load(7,1,25,4)); emit(addi(2,0,-128));
    emit(store_insn(2,1,31,0)); emit(load(12,1,31,0));
    emit(load(13,1,31,4)); emit(load(14,1,24,1));
    emit(load(15,1,28,2)); emit(load(16,1,28,6));
    emit(load(8,1,128,3)); emit(addi(9,0,99));
    emit(load(10,1,24,3)); emit(addi(17,0,111));
    emit(load(11,1,192,3)); emit(jal(0,12));
    emit(store_insn(2,1,24,3)); emit(addi(3,0,999));
    emit(addi(18,11,1)); emit(store_insn(18,1,256,3));
    emit(load(19,1,256,3)); emit(store_insn(19,1,512,3));
    emit(load(20,1,512,3)); emit(load(21,1,24,3));
    // Exercise every AMO in both widths, all aq/rl settings, and both word lanes.
    emit(addi(24,0,768)); emit(addi(25,0,-17));
    for(int width=2;width<=3;width++) begin
      for(int variant=0;variant<2;variant++) for(int op=0;op<9;op++) begin
        emit(addi(25,0,variant==0 ? -17 : 17)); emit(addi(2,0,variant==0 ? 7 : -128));
        emit(store_insn(25,24,0,width));
        emit(atomic_insn(atomic_ops[op],width,26,24,2,op%4));
        emit(addi(29,0,9)); emit(load(28,24,0,width)); emit(addi(27,26,1));
      end
      emit(atomic_insn(2,width,26,24,0,0)); emit(addi(29,0,3));
      emit(atomic_insn(3,width,26,24,2,1)); emit(addi(27,26,1));
      emit(atomic_insn(3,width,26,24,2,2)); emit(addi(27,26,1));
    end
    emit(addi(24,24,4));
    emit(atomic_insn(1,2,0,24,25)); emit(load(26,24,0,2));
    emit(atomic_insn(2,2,26,24,0)); emit(atomic_insn(3,2,0,24,2)); emit(load(26,24,0,2));
    // A different word and a different width cannot satisfy an exact reservation.
    emit(atomic_insn(2,2,26,24,0)); emit(addi(24,24,4));
    emit(atomic_insn(3,2,26,24,2)); emit(addi(27,26,1));
    emit(atomic_insn(2,2,26,24,0)); emit(atomic_insn(3,3,26,24,2));
    // An intervening same-line store invalidates LR even if it writes another byte.
    emit(atomic_insn(2,3,26,24,0)); emit(store_insn(2,24,9,0));
    emit(atomic_insn(3,3,26,24,2)); emit(addi(27,26,1));
    emit(addi(24,0,768)); probe_lr_pc=program_size*4;
    emit(atomic_insn(2,3,26,24,0)); probe_sc_pc=program_size*4;
    emit(atomic_insn(3,3,26,24,2)); emit(addi(27,26,1));
    emit(load(26,24,0,3));
    // Exhaustive intra-word offsets, both extension modes, cross-word and
    // cross-line fragments, x0 loads, and preservation of neighboring bytes.
    for(int width=1;width<=3;width++) for(int offset=1;offset<=7;offset++) if(offset%(1<<width)!=0) begin
      emit(store_insn(2,1,56+offset,width));
      emit(load(3,1,56+offset,width)); emit(addi(4,3,1));
      if(width<3) emit(load(5,1,56+offset,width+4));
      emit(load(0,1,56+offset,width));
      emit(load(6,1,56,3)); emit(load(7,1,64,3));
    end
    // Zero cold and owned-hit blocks, using deliberately unaligned rs1 values.
    // Observe every byte plus neighboring blocks through normal load retirement.
    for(int scenario=0;scenario<2;scenario++) begin
      emit(addi(24,0,scenario==0 ? 1027 : 1151));
      if(scenario==1) emit(store_insn(2,1,1088,3));
      emit(32'h0040200f | (24<<15)); emit(addi(29,0,9));
      for(int word=0;word<10;word++) emit(load(26,1,(scenario==0 ? 1016 : 1080)+word*8,3));
    end
    // Kill a CBO before WB; its entire target must remain unchanged.
    emit(addi(24,0,1215)); emit(jal(0,8)); emit(32'h0040200f | (24<<15));
    for(int word=0;word<8;word++) emit(load(26,1,1152+word*8,3));
    // Dirty lines publish data through Home-initiated self snoops. The shared
    // RN drops dirty copies even on CLEAN; CLEAN retains an already-clean copy.
    for(int operation=0;operation<3;operation++) begin
      emit(addi(24,0,1280+64*operation)); emit(addi(2,0,91+operation));
      emit(store_insn(2,24,0,3)); emit(addi(24,24,63));
      emit((32'(operation)<<20)|32'h0000200f|(24<<15));
      emit(load(26,1,1280+64*operation,3)); emit(addi(29,0,9));
      if(operation==1) begin
        clean_resident_pc=program_size*4;
        emit(32'h0010200f|(24<<15)); emit(load(26,1,1344,3));
      end
    end
    // Static device and noncacheable RAM blocks complete locally: neither
    // CHI port is permitted to send a synthetic data read/write.
    for(int region=0;region<2;region++) begin
      emit(32'h000022b7); // LUI x5,2
      emit(addi(24,5,region==0 ? 63 : 831));
      for(int operation=0;operation<3;operation++) emit((32'(operation)<<20)|32'h0000200f|(24<<15));
    end
    // Establish a fresh miss immediately before a fault: accepted work must drain first.
    emit(load(22,1,704,3)); emit(addi(23,0,2047));
    emit(addi(23,23,2047)); emit(addi(23,23,-1));
    emit(load(0,23,0,3)); emit(store_insn(2,1,24,3));
    repeat (4) @(negedge clock);
    reset = 0;
    wait(fault_seen);
    repeat (8) @(negedge clock);
    assert (reads >= 6 && writes > 0 && completions >= 6 && dual_commits > 0 && branches == 2 && replays > 0 && zero_commits==2)
      else $fatal(1, "missing cache scenario reads=%0d writes=%0d completions=%0d dual=%0d branches=%0d replays=%0d", reads,writes,completions,dual_commits,branches,replays);
    assert (hits_during_miss > 0 && alu_during_miss > 0) else $fatal(1, "no hit/ALU overlap with refill");
    assert(atomic_commits==53 && atomic_dual>0 && sc_success==3 && sc_failure==6 && probe_complete)
      else $fatal(1,"atomic coverage ops=%0d dual=%0d SC success=%0d failure=%0d probe=%b",atomic_commits,atomic_dual,sc_success,sc_failure,probe_complete);
    assert(split_resumes==71 && maintenance_commits==10 && maintenance_requests==4 && maintenance_responses==4 && !check_maintenance_completion && !check_maintenance_hit)
      else $fatal(1,"missing retained/maintenance coverage resumes=%0d commits=%0d requests=%0d responses=%0d",split_resumes,maintenance_commits,maintenance_requests,maintenance_responses);
    $display("RV2Wide shared L1D passed: %0d retirements, %0d refills, %0d writebacks, %0d replays, %0d warm hits during miss", commits, reads, writes, replays, hits_during_miss);
    $finish;
  end
endmodule
