// Checks vector memory against real MMU/cache execution, including tagged responses and precise restart.
// SPDX-License-Identifier: Apache-2.0
`include "tests/backend/verilog/rv5stage-memory-writeback.svh"
`ifndef RV5STAGE_VECTOR_COMPLETION_SLOTS
`define RV5STAGE_VECTOR_COMPLETION_SLOTS 8
`endif
module rv5stage_vector_memory_tb;
  localparam int COMPLETION_SLOTS = `RV5STAGE_VECTOR_COMPLETION_SLOTS;
  typedef struct packed {logic ready;} ready_t;
  typedef struct packed {logic [63:0] address;} ireq_bits_t;
  typedef struct packed {logic valid; ireq_bits_t bits;} ireq_t;
  typedef struct packed {logic [31:0] word; logic page_fault, access_fault;} iresp_bits_t;
  typedef struct packed {logic valid; iresp_bits_t bits;} iresp_t;
  typedef struct packed {ready_t request; iresp_t response;} instruction_in_t;
  typedef struct packed {logic flush, invalidate_all; ireq_t request; ready_t response;} instruction_out_t;
  typedef struct packed {
    logic [63:0] address;
    logic [3:0] access, atomic;
    logic [1:0] width;
    logic unsigned_0;
    logic [63:0] data;
    logic [8:0] writeback;
    logic [2:0] locality;
  } request_bits_t;
  typedef struct packed {request_bits_t request; logic device;} ureq_bits_t;
  typedef struct packed {logic valid; ureq_bits_t bits;} ureq_t;
  typedef struct packed {logic access_fault; logic [63:0] data; logic [8:0] writeback;} response_bits_t;
  typedef struct packed {logic valid; response_bits_t bits;} response_t;
  typedef struct packed {ready_t request; logic request_fault, request_access_fault; response_t response; logic drained;} uncached_in_t;
  typedef struct packed {ureq_t request;} uncached_out_t;
  typedef struct packed {logic valid; CHIReqFlit bits;} req_t;
  typedef struct packed {logic valid; CHIRspFlit bits;} rsp_t;
  typedef struct packed {logic valid; CHIDatFlit bits;} dat_t;
  typedef struct packed {logic valid; CHISnpFlit bits;} snp_t;
  typedef struct packed {ready_t requests, requester_responses, request_data; rsp_t responses; dat_t response_data; snp_t snoops;} chi_in_t;
  typedef struct packed {req_t requests; rsp_t requester_responses; dat_t request_data; ready_t responses, response_data, snoops;} chi_out_t;

  logic clock=0, reset=1;
  instruction_in_t instruction_in;
  instruction_out_t instruction_out;
  uncached_in_t uncached_in;
  uncached_out_t uncached_out;
  chi_in_t chi_in;
  chi_out_t chi_out;
  logic load_issue, load_hit, transaction_valid, transaction_fire;
  logic [63:0] load_address, cache_address;
  request_bits_t transaction;
  logic demand_attempt, demand_fire, cache_fire, permit_demand = 0;
  RV5StageLoadHit dut(.*);
  always #5 clock = ~clock;

  logic [31:0] program_words[2048];
  byte unsigned memory[32768];
  logic [63:0] expected[256];
  int pc = 0, expected_count = 0, signatures = 0, cycles = 0;
  int hits = 0, warm_run = 0, longest_warm_run = 0, rejections = 0;
  int overlapping_hits = 0, scalar_overlap = 0;
  int refills = 0, copybacks = 0, fault_signature = 0, device_elements = 0;
  bit instruction_valid = 0, uncached_pending = 0, returning = 0, writing_back = 0;
  logic [31:0] instruction_word;
  response_bits_t uncached_response;
  logic [63:0] line_address;
  logic [11:0] txn;
  int beat = 0, delay_cycles = 0, response_phase = 0, uncached_delay = 0;
  bit resumed = 0, vector_load_pending = 0;

  function automatic logic [31:0] addi(input int rd, rs, imm);
    return {12'(imm), 5'(rs), 3'b000, 5'(rd), 7'h13};
  endfunction
  function automatic logic [31:0] csr(input int address, rd, rs, op = 1);
    return {12'(address), 5'(rs), 3'(op), 5'(rd), 7'h73};
  endfunction
  function automatic logic [31:0] vmem(input bit store, input int width, regno, base, input bit masked = 0, strided = 0, input int stride = 0);
    return {4'b0, strided ? 2'b10 : 2'b00, !masked, 5'(strided ? stride : 0), 5'(base), 3'(width == 0 ? 0 : width + 4), 5'(regno), store ? 7'h27 : 7'h07};
  endfunction
  function automatic logic [31:0] indexed_vmem(input bit store, ordered, input int index_width, regno, base, index_reg, input bit masked = 0);
    return {4'b0, ordered ? 2'b11 : 2'b01, !masked, 5'(index_reg), 5'(base), 3'(index_width == 0 ? 0 : index_width + 4), 5'(regno), store ? 7'h27 : 7'h07};
  endfunction
  function automatic logic [31:0] segment_vmem(input bit store, input int width, fields, regno, base, input bit masked = 0, strided = 0, input int stride = 0);
    return {3'(fields-1), 1'b0, strided ? 2'b10 : 2'b00, !masked, 5'(strided ? stride : 0), 5'(base), 3'(width == 0 ? 0 : width + 4), 5'(regno), store ? 7'h27 : 7'h07};
  endfunction
  function automatic logic [31:0] indexed_segment_vmem(input bit store, ordered, input int index_width, fields, regno, base, index_reg, input bit masked = 0);
    return {3'(fields-1), 1'b0, ordered ? 2'b11 : 2'b01, !masked, 5'(index_reg), 5'(base), 3'(index_width == 0 ? 0 : index_width + 4), 5'(regno), store ? 7'h27 : 7'h07};
  endfunction
  function automatic logic [31:0] fault_only_first_vmem(input int width, fields, regno, base, input bit masked = 0);
    return {3'(fields-1), 1'b0, 2'b00, !masked, 5'd16, 5'(base), 3'(width == 0 ? 0 : width + 4), 5'(regno), 7'h07};
  endfunction
  function automatic logic [31:0] vint(input int op, vd, vs2, vs1, mode = 0);
    return {6'(op), 1'b1, 5'(vs2), 5'(vs1), 3'(mode), 5'(vd), 7'h57};
  endfunction
  function automatic logic [63:0] read_word(input int address);
    logic [63:0] value = 0;
    for (int b = 0; b < 8; b++) value[b*8+:8] = memory[address+b];
    return value;
  endfunction
  task automatic write_word(input int address, input logic [63:0] value);
    for (int b = 0; b < 8; b++) memory[address+b] = value[b*8+:8];
  endtask
  task automatic emit(input logic [31:0] instruction);
    program_words[pc++] = instruction;
  endtask
  task automatic li(input int rd, value);
    emit({20'((value + 2048) >> 12), 5'(rd), 7'h37});
    emit(addi(rd, rd, value));
  endtask
  task automatic configure(input int sew, lmul, vl);
    emit(addi(6, 0, vl));
    emit({1'b0, 11'((sew << 3) | lmul), 5'd6, 3'b111, 5'd0, 7'h57});
  endtask
  task automatic signature(input int regno, input logic [63:0] value);
    int offset = expected_count * 8;
    expected[expected_count++] = value;
    emit({7'(offset >> 5), 5'(regno), 5'd20, 3'b011, 5'(offset), 7'h23});
    emit(32'h0ff0000f);
  endtask
  task automatic check_memory(input int base, bytes, input logic [63:0] values[]);
    li(9, base);
    for (int wordno = 0; wordno < bytes/8; wordno++) begin
      emit({12'(wordno*8), 5'd9, 3'b011, 5'd7, 7'h03});
      signature(7, values[wordno]);
    end
  endtask
  task automatic check_strided_memory(input int base, stride, count, input logic [63:0] values[]);
    li(9, base);
    for (int wordno = 0; wordno < count; wordno++) begin
      emit({12'(wordno*stride), 5'd9, 3'b011, 5'd7, 7'h03});
      signature(7, values[wordno]);
    end
  endtask

  always_comb begin
    instruction_in = '0;
    instruction_in.request.ready = instruction_out.flush || !instruction_valid;
    instruction_in.response.valid = instruction_valid;
    instruction_in.response.bits.word = instruction_word;
    uncached_in = '0;
    uncached_in.request.ready = !uncached_pending;
    uncached_in.response.valid = uncached_pending && uncached_delay == 0;
    uncached_in.response.bits = uncached_response;
    uncached_in.drained = !uncached_pending;
    chi_in = '0;
    chi_in.requests.ready = cycles % 4 != 0 && !returning && !writing_back && response_phase == 0;
    chi_in.responses.valid = response_phase != 0;
    chi_in.responses.bits.opcode = response_phase == 1 ? 5'h03 : response_phase == 2 ? 5'h07 : 5'h05;
    chi_in.responses.bits.src_id = 7'd1;
    chi_in.responses.bits.tgt_id = 7'd3;
    chi_in.responses.bits.txn_id = txn;
    chi_in.responses.bits.dbid_or_group_id = 12'h55;
    chi_in.responses.bits.pcrd_type = 4'd2;
    chi_in.requester_responses.ready = cycles % 3 != 0;
    chi_in.request_data.ready = cycles % 3 != 0;
    chi_in.response_data.valid = returning && delay_cycles == 0 && cycles % 3 != 0;
    chi_in.response_data.bits.opcode = 4'h4;
    chi_in.response_data.bits.resp = 3'b010;
    chi_in.response_data.bits.byte_enable = 16'hffff;
    chi_in.response_data.bits.data_id = 2'(beat);
    chi_in.response_data.bits.home_nid_or_pbha_or_mismatched_mecid = 7'd1;
    chi_in.response_data.bits.dbid_or_mecid = 16'h55;
    chi_in.response_data.bits.txn_id = txn;
    chi_in.response_data.bits.src_id = 7'd1;
    chi_in.response_data.bits.tgt_id = 7'd3;
    chi_in.response_data.bits.data = {read_word(int'(line_address) + 16*beat + 8), read_word(int'(line_address) + 16*beat)};
  end
  always @(posedge clock) begin
    cycles <= cycles + 1;
    if (reset) begin
      instruction_valid <= 0;
      permit_demand <= 0;
    end else begin
      // One rejection per transaction, followed by persistent readiness: no
      // artificial periodic readiness/replay phase lock.
      if (demand_attempt) begin
        if (!permit_demand) begin permit_demand <= 1; rejections <= rejections + 1; end
        else if (demand_fire) permit_demand <= 0;
      end
      if (instruction_out.flush) instruction_valid <= 0;
      else if (instruction_in.response.valid && instruction_out.response.ready) instruction_valid <= 0;
      if (instruction_out.request.valid && instruction_in.request.ready) begin
        instruction_valid <= 1;
        instruction_word <= program_words[int'(instruction_out.request.bits.address / 4) % 2048];
      end
      if (load_hit) begin
        if (returning && line_address == 64'h1540) overlapping_hits <= overlapping_hits + 1;
        hits <= hits + 1;
        warm_run <= warm_run + 1;
        if (warm_run + 1 > longest_warm_run) longest_warm_run <= warm_run + 1;
      end else warm_run <= 0;
      if (resumed && load_issue)
        assert (load_address != 64'h4ff0 && load_address != 64'h4ff8)
          else $fatal(1, "fault restart repeated an authorized prefix element");
      if (load_issue && load_address == 64'h1300 && vector_load_pending) scalar_overlap <= scalar_overlap + 1;
      if (device_elements != 0 && load_issue && load_address == 64'h1308)
        assert (device_elements == 4 && !uncached_pending) else $fatal(1, "scalar load passed undrained vector stores");
      if (transaction_fire && transaction.access == 2 && transaction.address == 64'h2700)
        assert (!vector_load_pending) else $fatal(1, "scalar store passed an incomplete vector load");
      if (chi_out.requests.valid && chi_in.requests.ready) begin
        assert (chi_out.requests.bits.opcode inside {7'h02,7'h07,7'h1b})
          else $fatal(1, "unexpected CHI opcode %h", chi_out.requests.bits.opcode);
        line_address <= 64'(chi_out.requests.bits.address);
        txn <= chi_out.requests.bits.txn_id;
        if (chi_out.requests.bits.allow_retry) response_phase <= 1;
        else if (chi_out.requests.bits.opcode == 7'h1b) begin
          writing_back <= 1; response_phase <= 3; copybacks <= copybacks + 1; beat <= 0;
        end else begin
          returning <= 1; beat <= 0; delay_cycles <= 19; refills <= refills + 1;
        end
      end
      if (chi_in.responses.valid && chi_out.responses.ready) response_phase <= response_phase == 1 ? 2 : 0;
      if (delay_cycles > 0) delay_cycles <= delay_cycles - 1;
      if (chi_in.response_data.valid && chi_out.response_data.ready) begin
        if (beat == 3) returning <= 0; else beat <= beat + 1;
      end
      if (chi_out.request_data.valid && chi_in.request_data.ready) begin
        assert(writing_back) else $fatal(1, "unowned writeback");
        for (int b = 0; b < 16; b++)
          if (chi_out.request_data.bits.byte_enable[b])
            memory[int'(line_address) + 16*int'(chi_out.request_data.bits.data_id) + b] <= chi_out.request_data.bits.data[b*8+:8];
        if (beat == 3) writing_back <= 0; else beat <= beat + 1;
      end
      if (uncached_delay > 0) uncached_delay <= uncached_delay - 1;
      if (uncached_in.response.valid) begin uncached_pending <= 0; vector_load_pending <= 0; end
      if (uncached_out.request.valid && uncached_in.request.ready) begin
        uncached_pending <= 1; uncached_delay <= 11;
        uncached_response <= '{access_fault:0, data:64'h31,
          writeback:uncached_out.request.bits.request.writeback};
        if (uncached_out.request.bits.request.address == 64'ha000) begin
          assert (uncached_out.request.bits.request.access == 1 && uncached_out.request.bits.request.writeback[8:7] == 3)
            else $fatal(1, "expected vector uncached load");
          vector_load_pending <= 1; uncached_delay <= 50;
        end else if (uncached_out.request.bits.request.address >= 64'h9000) begin
          assert (uncached_out.request.bits.request.access == 2 &&
                  uncached_out.request.bits.request.address == 64'h9000 + 64'(device_elements*8) &&
                  uncached_out.request.bits.request.data == 64'(device_elements+1))
            else $fatal(1, "duplicate, unordered, or corrupt vector device store");
          device_elements <= device_elements + 1;
        end else begin
          assert (uncached_out.request.bits.request.access == 2 &&
                  uncached_out.request.bits.request.address == 64'h8000 + 64'(signatures*8) &&
                  uncached_out.request.bits.request.data == expected[signatures])
            else $fatal(1, "signature %0d addr=%h got=%h expected=%h", signatures,
                        uncached_out.request.bits.request.address, uncached_out.request.bits.request.data, expected[signatures]);
          signatures <= signatures + 1;
          if (signatures == fault_signature + 3) resumed <= 1;
          if (signatures + 1 == expected_count) begin
            assert ((COMPLETION_SLOTS < 8 || (longest_warm_run >= 8 && overlapping_hits > 0)) && scalar_overlap > 0 && rejections > 8 && device_elements == 4)
              else $fatal(1, "missing throughput, replay, or ordering coverage: run=%0d reject=%0d devices=%0d scalar_overlap=%0d", longest_warm_run,rejections,device_elements,scalar_overlap);
            $display("Vector memory (%0d slots): %0d signatures, %0d hits, %0d-cycle hit run, %0d rejections, %0d refills; strided/indexed/segmented/fault-only-first/masked/EEW/vstart/device/fault restart passed",
                     COMPLETION_SLOTS,expected_count,hits,longest_warm_run,rejections,refills);
            $finish;
          end
        end
      end
      assert (cycles < 60000) else $fatal(1, "vector memory timeout: signatures=%0d/%0d hits=%0d", signatures,expected_count,hits);
    end
  end

  initial begin
    logic [63:0] values[], field_values[];
    int fault_pc, continuation, before_handler, fof_fault_pc, fof_continuation, before_fof_handler, offsets[4];
    for (int i = 0; i < 2048; i++) program_words[i] = 32'h0000006f;
    for (int i = 0; i < 32768; i++) memory[i] = 8'h55;
    li(20, 'h8000); li(1, 'h600); emit(csr('h300,0,1)); li(1,'h1800); emit(csr('h305,0,1));
    for (int sew = 0; sew < 4; sew++) begin
      values = new[2 << sew];
      for (int i = 0; i < 16; i++) begin
        for (int b = 0; b < (1 << sew); b++) memory['h1000+sew*256+(i<<sew)+b] = 8'((i+1) >> (8*b));
        for (int b = 0; b < (1 << sew); b++) values[(i<<sew)/8][(((i<<sew)+b)%8)*8+:8] = 8'((i+2) >> (8*b));
      end
      configure(sew,3,16);
      li(8,'h1000+sew*256); li(9,'h2000+sew*256);
      emit(vmem(0,sew,8,8));
      emit(vint(0,16,8,1,3)); // packed vadd.vi, between memory macros
      emit(vmem(1,sew,16,9));
      check_memory('h2000+sew*256,16<<sew,values);
      // Warm-cache vector loads must sustain one element per cycle.
      emit(vmem(0,sew,8,8));
    end
    // Cold first element followed by seven resident elements: complete hits
    // ahead of an older delayed miss, then drain the tagged results in order.
    values = new[8];
    for (int i = 0; i < 8; i++) begin
      values[i] = 64'(101+i); write_word('h1578+i*8,values[i]);
    end
    li(8,'h1580); emit({12'b0,5'd8,3'b011,5'd7,7'h03}); emit(32'h0ff0000f);
    configure(3,2,8); li(8,'h1578); li(9,'h2600);
    emit(vmem(0,3,8,8)); emit(vmem(1,3,8,9));
    check_memory('h2600,64,values);
    // EEW differs from SEW: EMUL=4, not LMUL=8.
    configure(1,3,16); li(8,'h1000); emit(vmem(0,0,8,8));
    configure(0,0,16);
    emit(vint(28,0,8,8,3)); // vmsleu.vi: enable the first eight elements
    emit(vint(11,16,16,16)); // clear v16 with vxor
    li(7,'h55); emit(vint(0,16,16,7,4));
    emit(csr(8,0,3,5)); // vstart=3
    emit(vmem(0,0,16,8,1));
    li(9,'h2400); emit(vmem(1,0,16,9));
    values = new[2]; values[0]=64'h0807060504555555; values[1]=64'h5555555555555555;
    check_memory('h2400,16,values);
    // Strided memory advances a captured full address. Positive, negative,
    // zero, and nonzero-vstart cases exercise the incremental sequencer.
    values = new[4];
    for (int i = 0; i < 4; i++) begin values[i] = 64'('h301+i); write_word('h2800+i*16,values[i]); end
    configure(3,1,4); li(8,'h2800); li(9,'h2a00); li(10,16);
    emit(vmem(0,3,8,8,0,1,10)); emit(vmem(1,3,8,9));
    check_memory('h2a00,32,values);
    li(9,'h2b00); li(10,16); emit(vmem(1,3,8,9,0,1,10));
    check_strided_memory('h2b00,16,4,values);
    for (int i = 0; i < 4; i++) values[i] = 64'('h304-i);
    li(8,'h2830); li(9,'h2a40); li(10,-16);
    emit(vmem(0,3,8,8,0,1,10)); emit(vmem(1,3,8,9));
    check_memory('h2a40,32,values);
    for (int i = 0; i < 4; i++) values[i] = 64'h301;
    li(8,'h2800); li(9,'h2a80); emit(vmem(0,3,8,8,0,1,0)); emit(vmem(1,3,8,9));
    check_memory('h2a80,32,values);
    emit(vint(11,16,16,16)); li(7,'h55); emit(vint(0,16,16,7,4));
    emit(csr(8,0,2,5));
    li(8,'h2800); li(9,'h2ac0); li(10,16); emit(vmem(0,3,16,8,0,1,10)); emit(vmem(1,3,16,9));
    values[0]=64'h55; values[1]=64'h55; values[2]=64'h303; values[3]=64'h304;
    check_memory('h2ac0,32,values);
    // Indexed memory uses encoded index EEW but vtype SEW for transferred data.
    // Nonmonotonic offsets also prove that addresses are base+offset, not scaled.
    values = new[4];
    for (int i = 0; i < 4; i++) write_word('h2d00+i*8,64'('h401+i));
    memory['h2c00] = 24; memory['h2c01] = 0;
    memory['h2c02] = 0;  memory['h2c03] = 0;
    memory['h2c04] = 16; memory['h2c05] = 0;
    memory['h2c06] = 8;  memory['h2c07] = 0;
    configure(3,1,4); li(8,'h2c00); emit(vmem(0,1,16,8));
    li(8,'h2d00); emit(indexed_vmem(0,0,1,8,8,16));
    li(9,'h2e00); emit(vmem(1,3,8,9));
    values[0]=64'h404; values[1]=64'h401; values[2]=64'h403; values[3]=64'h402;
    check_memory('h2e00,32,values);
    li(9,'h2e40); emit(indexed_vmem(1,1,1,8,9,16));
    values[0]=64'h401; values[1]=64'h402; values[2]=64'h403; values[3]=64'h404;
    check_memory('h2e40,32,values);
    // Unit-stride segments pipeline distinct field operations through the
    // completion window, map fields to consecutive EMUL groups, and advance
    // vstart in whole-segment units.
    values = new[12];
    for (int element = 0; element < 4; element++) begin
      for (int field = 0; field < 3; field++) begin
        values[element*3+field] = 64'('h500 + element*16 + field);
        write_word('h3100+(element*3+field)*8,values[element*3+field]);
      end
    end
    configure(2,0,4); li(8,'h3100); emit(segment_vmem(0,3,3,8,8));
    for (int field = 0; field < 3; field++) begin
      field_values = new[4];
      for (int element = 0; element < 4; element++) field_values[element] = values[element*3+field];
      li(9,'h3200+field*64); emit(vmem(1,3,8+field*2,9));
      check_memory('h3200+field*64,32,field_values);
    end
    li(9,'h3300); emit(segment_vmem(1,3,3,8,9));
    check_memory('h3300,96,values);
    emit(csr(8,0,1,5)); li(9,'h3400); emit(segment_vmem(1,3,3,8,9));
    values[0]=64'h5555555555555555; values[1]=64'h5555555555555555; values[2]=64'h5555555555555555;
    check_memory('h3400,96,values);
    emit(csr(8,7,0,2)); signature(7,0);
    // Constant-stride segments use one captured signed stride per segment while
    // keeping fields contiguous. Exercise positive, negative, zero, and vstart
    // progression with EEW=64/SEW=32 and EMUL=2 field groups.
    for (int element = 0; element < 4; element++) begin
      for (int field = 0; field < 3; field++) begin
        values[element*3+field] = 64'('h600 + element*16 + field);
        write_word('h3500+element*32+field*8,values[element*3+field]);
      end
    end
    li(8,'h3500); li(10,32); emit(segment_vmem(0,3,3,8,8,0,1,10));
    for (int field = 0; field < 3; field++) begin
      field_values = new[4];
      for (int element = 0; element < 4; element++) field_values[element] = values[element*3+field];
      li(9,'h3600+field*64); emit(vmem(1,3,8+field*2,9));
      check_memory('h3600+field*64,32,field_values);
    end
    li(9,'h3700); li(10,32); emit(segment_vmem(1,3,3,8,9,0,1,10));
    for (int field = 0; field < 3; field++) begin
      field_values = new[4];
      for (int element = 0; element < 4; element++) field_values[element] = values[element*3+field];
      check_strided_memory('h3700+field*8,32,4,field_values);
    end
    li(9,'h38c0); li(10,-32); emit(segment_vmem(1,3,3,8,9,0,1,10));
    for (int field = 0; field < 3; field++) begin
      field_values = new[4];
      for (int element = 0; element < 4; element++) field_values[element] = values[(3-element)*3+field];
      check_strided_memory('h3860+field*8,32,4,field_values);
    end
    li(9,'h3980); emit(segment_vmem(1,3,3,8,9,0,1,0));
    field_values = new[3];
    for (int field = 0; field < 3; field++) field_values[field] = values[9+field];
    check_memory('h3980,24,field_values);
    emit(csr(8,0,1,5)); li(9,'h3a00); li(10,32); emit(segment_vmem(1,3,3,8,9,0,1,10));
    for (int field = 0; field < 3; field++) begin
      field_values = new[4]; field_values[0] = 64'h5555555555555555;
      for (int element = 1; element < 4; element++) field_values[element] = values[element*3+field];
      check_strided_memory('h3a00+field*8,32,4,field_values);
    end
    emit(csr(8,7,0,2)); signature(7,0);
    // Indexed segments combine one vector byte offset per segment with
    // contiguous SEW-sized fields. Exercise mixed index/data EEW, both
    // ordering encodings, repeated ordered offsets, and segment-granular vstart.
    offsets[0]=72; offsets[1]=0; offsets[2]=48; offsets[3]=24;
    values = new[12];
    for (int element = 0; element < 4; element++) begin
      memory['h3b00+element*2] = 8'(offsets[element]);
      memory['h3b01+element*2] = 8'(offsets[element] >> 8);
      for (int field = 0; field < 3; field++) begin
        values[element*3+field] = 64'('h700 + element*16 + field);
        write_word('h3c00+offsets[element]+field*8,values[element*3+field]);
      end
    end
    configure(3,1,4); li(8,'h3b00); emit(vmem(0,1,16,8));
    li(8,'h3c00); emit(indexed_segment_vmem(0,0,1,3,8,8,16));
    for (int field = 0; field < 3; field++) begin
      field_values = new[4];
      for (int element = 0; element < 4; element++) field_values[element] = values[element*3+field];
      li(9,'h3d00+field*64); emit(vmem(1,3,8+field*2,9));
      check_memory('h3d00+field*64,32,field_values);
    end
    li(9,'h3e00); emit(indexed_segment_vmem(1,1,1,3,8,9,16));
    for (int element = 0; element < 4; element++) begin
      field_values = new[3];
      for (int field = 0; field < 3; field++) field_values[field] = values[element*3+field];
      check_memory('h3e00+offsets[element],24,field_values);
    end
    emit(vint(11,16,16,16)); li(9,'h3f00); emit(indexed_segment_vmem(1,1,1,3,8,9,16));
    field_values = new[3];
    for (int field = 0; field < 3; field++) field_values[field] = values[9+field];
    check_memory('h3f00,24,field_values);
    li(8,'h3b00); emit(vmem(0,1,16,8)); emit(csr(8,0,1,5)); li(9,'h3f80); emit(indexed_segment_vmem(1,1,1,3,8,9,16));
    for (int element = 0; element < 4; element++) begin
      field_values = new[3];
      for (int field = 0; field < 3; field++) field_values[field] = element == 0 ? 64'h5555555555555555 : values[element*3+field];
      check_memory('h3f80+offsets[element],24,field_values);
    end
    emit(csr(8,7,0,2)); signature(7,0);
    // Empty and fully masked bodies must not touch an unmapped address.
    li(8,'h10000); emit(csr(8,0,20,5)); emit(vmem(0,0,16,8));
    emit(vint(25,0,8,8)); // vmsne.vv v0,v8,v8
    emit(vmem(0,0,16,8,1)); emit(vmem(1,0,16,8,1));
    li(10,32); emit(segment_vmem(0,3,3,16,8,1,1,10)); emit(segment_vmem(1,3,3,16,8,1,1,10));
    emit(indexed_segment_vmem(0,1,1,3,8,8,16,1)); emit(indexed_segment_vmem(1,1,1,3,8,8,16,1));
    emit(fault_only_first_vmem(3,1,8,8,1)); emit(fault_only_first_vmem(3,3,8,8,1));
    emit(csr(8,7,0,2)); signature(7,0);
    // A scalar load may pass an older vector load's delayed completion, but
    // the following scalar store must wait for that vector load to drain.
    configure(3,0,1); li(8,'ha000); li(9,'h1300); li(10,'h2700);
    emit(vmem(0,3,8,8));
    emit({12'b0,5'd9,3'b011,5'd7,7'h03});
    emit({7'b0,5'd7,5'd10,3'b011,5'b0,7'h23});
    signature(7,1);
    // Exactly-once vector stores through the uncached/device LSU path.
    configure(3,1,4); li(8,'h1300); emit(vmem(0,3,8,8)); li(9,'h9000); emit(vmem(1,3,8,9));
    emit({12'd8,5'd8,3'b011,5'd7,7'h03}); signature(7,2);
    // Fault-only-first loads serialize unresolved elements. A later page fault
    // truncates VL without trapping; a fault in segment zero remains precise.
    write_word('h3000,64'h1001); write_word('h4000,64'h1401);
    write_word('h5020,64'h4c7); write_word('h5028,0);
    write_word('h1fd8,64'h31); write_word('h1fe0,64'h32);
    write_word('h1fe8,64'h33); write_word('h1ff0,64'h21); write_word('h1ff8,64'h22);
    li(7,1); emit({6'b0,6'd63,5'd7,3'b001,5'd7,7'h13}); emit(addi(7,7,3)); emit(csr('h180,0,7));
    configure(3,1,4); li(8,'h4ff8); li(1,'h20e00); emit(csr('h300,0,1)); emit(fault_only_first_vmem(3,1,8,8));
    li(1,'h600); emit(csr('h300,0,1)); emit(csr('hc20,7,0,2)); signature(7,1); emit(csr(8,7,0,2)); signature(7,0);
    li(9,'h2800); emit(vmem(1,3,8,9)); values=new[1]; values[0]=64'h22; check_memory('h2800,8,values);
    configure(3,1,4); li(8,'h4fd8); li(1,'h20e00); emit(csr('h300,0,1)); emit(fault_only_first_vmem(3,2,8,8));
    li(1,'h600); emit(csr('h300,0,1)); emit(csr('hc20,7,0,2)); signature(7,2); emit(csr(8,7,0,2)); signature(7,0);
    li(9,'h2820); emit(vmem(1,3,8,9)); values=new[2]; values[0]=64'h31; values[1]=64'h33; check_memory('h2820,16,values);
    li(9,'h2840); emit(vmem(1,3,10,9)); values[0]=64'h32; values[1]=64'h21; check_memory('h2840,16,values);
    configure(3,1,4); li(1,'h1a00); emit(csr('h305,0,1)); li(8,'h5000); li(1,'h20e00); emit(csr('h300,0,1));
    fof_fault_pc=pc*4; emit(fault_only_first_vmem(3,1,8,8)); fof_continuation=pc;
    before_fof_handler=pc; pc=1664;
    li(1,'h600); emit(csr('h300,0,1));
    emit(csr('h341,7,0,2)); signature(7,64'(fof_fault_pc));
    emit(csr('h342,7,0,2)); signature(7,13); emit(csr('h343,7,0,2)); signature(7,'h5000);
    emit(csr('hc20,7,0,2)); signature(7,4); emit(csr(8,7,0,2)); signature(7,0);
    li(10,fof_continuation*4); emit({12'b0,5'd10,3'b0,5'd0,7'h67});
    pc=before_fof_handler; li(1,'h1800); emit(csr('h305,0,1));
    // An indexed segmented load crosses an Sv39 leaf boundary after one whole segment.
    for (int element = 0; element < 4; element++) begin
      memory['h2f00+element*2] = 8'(element*16);
      memory['h2f01+element*2] = 0;
    end
    li(8,'h2f00); emit(vmem(0,1,16,8));
    write_word('h1ff0,64'h21); write_word('h1ff8,64'h22);
    for (int i = 0; i < 6; i++) write_word('h6000+i*8,64'('h23+i));
    li(7,1); emit({6'b0,6'd63,5'd7,3'b001,5'd7,7'h13}); emit(addi(7,7,3)); emit(csr('h180,0,7));
    li(8,'h4ff0); li(1,'h20e00); emit(csr('h300,0,1));
    fault_pc=pc*4; emit(indexed_segment_vmem(0,1,1,2,8,8,16)); continuation=pc;
    li(1,'h600); emit(csr('h300,0,1));
    li(9,'h2500); emit(vmem(1,3,8,9)); li(9,'h2540); emit(vmem(1,3,10,9));
    // Handler signatures precede these continuation signatures.
    before_handler=pc; pc=1536;
    li(1,'h600); emit(csr('h300,0,1));
    fault_signature=expected_count;
    emit(csr('h342,7,0,2)); signature(7,13);
    emit(csr('h343,7,0,2)); signature(7,'h5000);
    emit(csr(8,7,0,2)); signature(7,1);
    emit(csr('h341,10,0,2)); signature(10,64'(fault_pc));
    li(9,'h5028); li(7,'h18c7); emit({7'b0,5'd7,5'd9,3'b011,5'b0,7'h23});
    emit(32'h0ff0000f); emit(32'h12000073);
    li(8,'h4ff0); li(1,'h20e00); emit(csr('h300,0,1)); emit({12'b0,5'd10,3'b0,5'd0,7'h67});
    pc=before_handler;
    values=new[4]; values[0]=64'h21; values[1]=64'h23; values[2]=64'h25; values[3]=64'h27; check_memory('h2500,32,values);
    values[0]=64'h22; values[1]=64'h24; values[2]=64'h26; values[3]=64'h28; check_memory('h2540,32,values);
    emit(csr(8,7,0,2)); signature(7,0);
    emit(32'h0000006f);
    assert(continuation < 1536) else $fatal(1,"program overlaps handler");
    repeat(4) @(negedge clock);
    reset=0;
  end
endmodule
