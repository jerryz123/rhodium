// SPDX-License-Identifier: Apache-2.0
module rv2wide_cache_tb;
  typedef struct packed { logic [63:0] cause, value; } fetch_fault_t;
  typedef struct packed { logic valid; fetch_fault_t bits; } fetch_fault_flow_t;
  typedef struct packed { logic [63:0] pc; logic [31:0] instruction; fetch_fault_flow_t fault; } instruction_t;
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
  chi_in_t chi_in;
  chi_out_t chi_out;
  RV2WideCacheFixture dut(.clock(clock), .reset(reset), .node_id(7'd3),
    .instructions_in(instructions), .instructions_out(instructions_ready),
    .retired_0_out(retired[0]), .retired_1_out(retired[1]), .completed_out(completed),
    .redirect_out(redirect), .issued(issued), .retired_count(retired_count), .chi_in(chi_in), .chi_out(chi_out));
  always #5 clock = ~clock;
  logic [31:0] program_words[128];
  byte unsigned backing[4096], reference_bytes[4096];
  logic [63:0] registers[32];
  retirement_t completion_queue[$];
  int program_size, send_pc, reference_pc, cycles, commits, dual_commits, replays, branches;
  int reads, writes, completions, hits_during_miss, alu_during_miss;
  bit fault_seen, read_active, write_active;
  CHIReqFlit pending_read, pending_write;
  int read_due, read_packet;

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
  task automatic emit(logic [31:0] instruction);
    program_words[program_size++] = instruction;
  endtask

  // A byte-addressed backing memory is independent of cache tags, ownership, and replacement.
  always_comb begin
    chi_in = '0;
    chi_in.requests.ready = !read_active && !write_active && cycles % 5 != 0;
    chi_in.requester_responses.ready = cycles % 4 != 0;
    chi_in.request_data.ready = cycles % 3 != 0;
    if (write_active) begin
      chi_in.responses.valid = 1;
      chi_in.responses.bits.opcode = 5'h05;
      chi_in.responses.bits.src_id = 7'd1;
      chi_in.responses.bits.tgt_id = 7'd3;
      chi_in.responses.bits.txn_id = pending_write.txn_id;
      chi_in.responses.bits.dbid_or_group_id = 12'd9;
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
      7'h13: begin value = registers[rs1] + 64'($signed(insn[31:20])); writes_rd = rd != 0; end
      7'h03: begin
        address = registers[rs1] + 64'($signed(insn[31:20]));
        bytes_count = 1 << (width & 3);
        for (int b = 0; b < bytes_count; b++) value[b*8 +: 8] = reference_bytes[int'(address)+b];
        if (width < 4 && bytes_count < 8 && value[bytes_count*8-1]) value |= ~64'd0 << (bytes_count*8);
        writes_rd = rd != 0;
        if (read_active && !got.deferred) hits_during_miss++;
      end
      7'h23: begin
        imm = 64'($signed({insn[31:25],insn[11:7]})); address = registers[rs1] + imm;
        for (int b = 0; b < (1 << width); b++) reference_bytes[int'(address)+b] = registers[rs2][b*8 +: 8];
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
    if (!reset && !fault_seen && send_pc/4 < program_size) begin
      instructions.valid = 1;
      instructions.bits.count = send_pc/4+1 < program_size ? 2 : 1;
      instructions.bits.entries[0] = '{64'(send_pc), program_words[send_pc/4], '0};
      instructions.bits.entries[1] = '{64'(send_pc+4), program_words[send_pc/4+1], '0};
    end
  end
  always @(posedge clock) if (!reset) begin
    cycles <= cycles + 1;
    if (cycles > 10000) $fatal(1, "cache/core timeout pc=%h reference=%h reads=%0d completions=%0d", send_pc, reference_pc, reads, completions);
    if (instructions.valid && instructions_ready) send_pc += 4*int'(instructions.bits.count);
    if (retired[0].valid && retired[1].valid) dual_commits++;
    for (int slot = 0; slot < 2; slot++) if (retired[slot].valid) check_retirement(retired[slot].bits);
    if (completed.valid) begin
      retirement_t expected;
      assert (completion_queue.size() > 0) else $fatal(1, "unexpected completion");
      expected = completion_queue.pop_front();
      assert (completed.bits.fetched == expected.fetched && completed.bits.rd == expected.rd && completed.bits.write == expected.write)
        else $fatal(1, "completion owner mismatch");
      if (expected.write) assert (completed.bits.data == expected.data) else $fatal(1, "load completion got=%h expected=%h at %h", completed.bits.data, expected.data, expected.fetched.pc);
      completions++;
    end
    if (redirect.valid) begin
      case (redirect.bits.resolution.disposition)
        0: begin send_pc = int'(redirect.bits.target); branches++; end
        2: begin send_pc = int'(redirect.bits.pc); replays++; end
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
        default: $fatal(1, "unexpected request opcode %h", chi_out.requests.bits.opcode);
      endcase
    end
    if (chi_in.response_data.valid && chi_out.response_data.ready) begin
      if ($test$plusargs("debug")) $display("%0d DAT packet=%0d", cycles, read_packet);
      if (read_packet == 3) read_active <= 0;
      else begin read_packet <= read_packet + 1; read_due <= cycles + 2; end
    end
    if (chi_out.requester_responses.valid && chi_in.requester_responses.ready && $test$plusargs("debug"))
      $display("%0d ACK", cycles);
    if (chi_in.responses.valid && chi_out.responses.ready) write_active <= 0;
    if (chi_out.request_data.valid && chi_in.request_data.ready) begin
      assert (chi_out.request_data.bits.opcode == 4'h2) else $fatal(1, "unexpected write data");
      for (int b = 0; b < 16; b++) if (chi_out.request_data.bits.byte_enable[b])
        backing[int'(pending_write.address)+16*int'(chi_out.request_data.bits.data_id)+b] = chi_out.request_data.bits.data[b*8 +: 8];
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
    // Establish a fresh miss immediately before a fault: accepted work must drain first.
    emit(load(22,1,704,3)); emit(addi(23,0,2047));
    emit(addi(23,23,2047)); emit(addi(23,23,2));
    emit(load(0,23,0,3)); emit(store_insn(2,1,24,3));
    repeat (4) @(negedge clock);
    reset = 0;
    wait(fault_seen);
    repeat (8) @(negedge clock);
    assert (reads >= 6 && writes > 0 && completions >= 6 && dual_commits > 0 && branches == 1 && replays > 0)
      else $fatal(1, "missing cache scenario reads=%0d writes=%0d completions=%0d dual=%0d branches=%0d replays=%0d", reads,writes,completions,dual_commits,branches,replays);
    assert (hits_during_miss > 0 && alu_during_miss > 0) else $fatal(1, "no hit/ALU overlap with refill");
    $display("RV2Wide shared L1D passed: %0d retirements, %0d refills, %0d writebacks, %0d replays, %0d warm hits during miss", commits, reads, writes, replays, hits_during_miss);
    $finish;
  end
endmodule
