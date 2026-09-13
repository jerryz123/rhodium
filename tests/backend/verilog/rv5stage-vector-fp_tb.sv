// Executes vector FP and scalar FP together through WB, checking ordered memory-visible results and flags.
// SPDX-License-Identifier: Apache-2.0
`include "tests/backend/verilog/rv5stage-memory-writeback.svh"
module rv5stage_vector_fp_tb;
  typedef struct packed {
    logic supervisor_software;
    logic machine_software;
    logic supervisor_timer;
    logic machine_timer;
    logic supervisor_external;
    logic machine_external;
  } interrupts_t;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic [63:0] address; } instruction_req_bits_t;
  typedef struct packed { logic valid; instruction_req_bits_t bits; } instruction_req_t;
  typedef struct packed { logic [31:0] word; logic page_fault; logic access_fault; } instruction_resp_bits_t;
  typedef struct packed { logic valid; instruction_resp_bits_t bits; } instruction_resp_t;
  typedef struct packed { ready_t request; instruction_resp_t response; } instruction_in_t;
  typedef struct packed {
    logic flush;
    logic invalidate_all;
    instruction_req_t request;
    ready_t response;
  } instruction_out_t;
  typedef struct packed {
    logic [63:0] address;
    logic [3:0] access;
    logic [3:0] atomic;
    logic [1:0] width;
    logic unsigned_0;
    logic [63:0] data;
    logic [8:0] writeback;
    logic [2:0] locality;
  } data_req_bits_t;
  typedef struct packed { logic valid; data_req_bits_t bits; } data_req_t;
  typedef struct packed {
    logic access_fault;
    logic [63:0] data;
    logic [8:0] writeback;
  } data_resp_bits_t;
  typedef struct packed { logic valid; data_resp_bits_t bits; } data_resp_t;
  typedef struct packed { ready_t request; logic request_fault; logic request_access_fault; data_resp_t response; logic drained; logic reservation_valid; } data_in_t;

  typedef struct packed { data_req_t request; } data_out_t;

  logic clock = 0, reset = 1;
  interrupts_t interrupts = '0;
  instruction_in_t instruction_access_in;
  instruction_out_t instruction_access_out;
  data_in_t data_access_in;
  data_out_t data_access_out;
  logic [63:0] hart_id = 0, time_counter = 0, mstatus, satp;
  logic [1:0] privilege;
  logic translation_flush;
  logic response_valid = 0;
  logic reject_request = 1;
  logic [31:0] response_word;
  logic [31:0] program_words [0:511];
  logic [63:0] memory_words [0:511];
  logic [63:0] expected_data [0:511], expected_address [0:511];
  integer expected_width [0:511];
  integer pc = 0, expected_count = 0, stores = 0, cycles = 0, load_delay = 0;
  data_resp_bits_t pending_load;
  RV5StageCoreFixture dut (.pipeline_access_in('0), .pipeline_access_out(), .prefetch_out(), .*);
  always #5 clock = ~clock;

  task automatic emit(input logic [31:0] word);
    program_words[pc++] = word;
  endtask
  task automatic li(input integer rd, input integer value);
    emit(32'((((value + 2048) >> 12) << 12) | (rd << 7) | 'h37));
    emit(32'(((value & 4095) << 20) | (rd << 15) | (rd << 7) | 'h13));
  endtask
  task automatic vset(input integer sew, input integer vl, input integer lmul = 0);
    emit(32'('hc0007057 | (sew << 23) | (lmul << 20) | (vl << 15)));
  endtask
  task automatic vec(input integer funct6, input integer rd, input integer vs2, input integer vs1, input bit masked = 0, input integer funct3 = 1);
    emit(32'((funct6 << 26) | (int'(!masked) << 25) | (vs2 << 20) | (vs1 << 15) | (funct3 << 12) | (rd << 7) | 'h57));
  endtask
  task automatic vload(input integer rd, input integer address, input integer width);
    li(10, address);
    emit(32'('h02050007 | ((width == 2 ? 6 : 7) << 12) | (rd << 7)));
  endtask
  task automatic expect_store(input integer address, input logic [63:0] value, input integer width);
    expected_address[expected_count] = 64'(address);
    expected_data[expected_count] = value;
    expected_width[expected_count++] = width;
  endtask
  task automatic vstore(input integer rd, input integer address, input integer width);
    li(10, address);
    emit(32'('h02050027 | ((width == 2 ? 6 : 7) << 12) | (rd << 7)));
  endtask
  task automatic signature(input integer csr, input integer address, input logic [63:0] value);
    emit(32'((csr << 20) | 'h000021f3)); // csrr x3,csr; all state observers must drain FP
    li(10, address);
    emit(32'h00353023); // sd x3,0(x10)
    expect_store(address, value, 3);
  endtask

  initial begin
    for (int i = 0; i < 512; i++) begin program_words[i] = 'h0000006f; memory_words[i] = 0; end
    li(1, 'h700); emit('h30509073); // trap vector: timeout reports a precise unexpected fault
    li(1, 'h2200); emit('h30009073); // FS and VS Initial
    vset(2, 4);
    memory_words[0] = 'h400000003f800000; // 1,2
    memory_words[1] = 'h4080000040400000; // 3,4
    memory_words[2] = 'hbf8000003f000000; // .5,-1
    memory_words[3] = 'hc000000040000000; // 2,-2
    vload(8, 'h1000, 2); vload(9, 'h1010, 2);
    vec(0, 10, 8, 9);
    // Independent scalar FP can compete while the vector completion tail is live.
    li(5, 'h3f800000); emit('hf00280d3); // fmv.w.x f1,x5
    emit('h0010f153); // fadd.s f2,f1,f1,dyn
    emit('he00101d3); // fmv.x.w x3,f2
    li(10, 'h2000); emit('h00353023); expect_store('h2000, 'h40000000, 3);
    vec(2, 11, 8, 9); vec('h24, 12, 8, 9);
    vstore(10, 'h2010, 2);
    expect_store('h2010, 'h3fc00000, 2); expect_store('h2014, 'h3f800000, 2);
    expect_store('h2018, 'h40a00000, 2); expect_store('h201c, 'h40000000, 2);
    vstore(11, 'h2020, 2);
    expect_store('h2020, 'h3f000000, 2); expect_store('h2024, 'h40400000, 2);
    expect_store('h2028, 'h3f800000, 2); expect_store('h202c, 'h40c00000, 2);
    vstore(12, 'h2030, 2);
    expect_store('h2030, 'h3f000000, 2); expect_store('h2034, 64'hc0000000, 2);
    expect_store('h2038, 'h40c00000, 2); expect_store('h203c, 64'hc1000000, 2);
    // In-place singleton writes must preserve the other FP32 half and pre-vstart lane.
    emit('h0080d073); // csrwi vstart,1
    vec(0, 8, 8, 9);
    vstore(8, 'h2040, 2);
    expect_store('h2040, 'h3f800000, 2); expect_store('h2044, 'h3f800000, 2);
    expect_store('h2048, 'h40a00000, 2); expect_store('h204c, 'h40000000, 2);
    signature('h008, 'h2050, 0);
    signature('h001, 'h2058, 0);
    vec('h1f, 0, 9, 0, 0, 3); // vmsgt.vi v0,v9,0: lanes 0 and 2 only
    vec(0, 8, 8, 9, 1);
    vstore(8, 'h2100, 2);
    expect_store('h2100, 'h3fc00000, 2); expect_store('h2104, 'h3f800000, 2);
    expect_store('h2108, 'h40e00000, 2); expect_store('h210c, 'h40000000, 2);
    // Double precision uses raw VRF operands, not scalar FPR boxing checks.
    vset(3, 2);
    memory_words[8] = 'h3ff0000000000000; memory_words[9] = 'h4000000000000000;
    memory_words[10] = 'h3fe0000000000000; memory_words[11] = 'hbff0000000000000;
    vload(8, 'h1040, 3); vload(9, 'h1050, 3);
    vec(0, 10, 8, 9); vec(2, 11, 8, 9); vec('h24, 12, 8, 9);
    vstore(10, 'h2060, 3); expect_store('h2060, 'h3ff8000000000000, 3); expect_store('h2068, 'h3ff0000000000000, 3);
    vstore(11, 'h2070, 3); expect_store('h2070, 'h3fe0000000000000, 3); expect_store('h2078, 'h4008000000000000, 3);
    vstore(12, 'h2080, 3); expect_store('h2080, 'h3fe0000000000000, 3); expect_store('h2088, 'hc000000000000000, 3);
    // Masked-off signaling NaNs neither execute nor contribute NV.
    vset(2, 4);
    memory_words[12] = 'h7f8000017f800001; memory_words[13] = 'h7f8000017f800001;
    vload(8, 'h1060, 2);
    vec('h18, 0, 8, 8, 0, 0); // vmseq.vv v0,v8,v8: all enabled
    vec('h19, 0, 8, 8, 0, 0); // vmsne.vv v0,v8,v8: all disabled
    vec(0, 10, 8, 8, 1);
    signature('h001, 'h2090, 0);
    emit('h0080006f); vec(0, 10, 8, 8); // branch-squashed NaN arithmetic
    signature('h001, 'h2098, 0);
    vec(0, 10, 8, 8);
    signature('h001, 'h20a0, 16);
    vstore(10, 'h20b0, 2);
    for (int i = 0; i < 4; i++) expect_store('h20b0 + i*4, 'h7fc00000, 2);
    emit('h00105073); // clear fflags
    // Captured RUP: 1 + 2^-24 rounds upward, setting NX.
    memory_words[14] = 'h3f8000003f800000; memory_words[15] = 'h3f8000003f800000;
    memory_words[16] = 'h3380000033800000; memory_words[17] = 'h3380000033800000;
    vload(8, 'h1070, 2); vload(9, 'h1080, 2);
    emit('h0021d073); // csrwi frm,3
    vec(0, 10, 8, 9);
    signature('h001, 'h20c0, 1);
    vstore(10, 'h20d0, 2);
    for (int i = 0; i < 4; i++) expect_store('h20d0 + i*4, 'h3f800001, 2);
    // More elements than completion slots: reuse tags while a scalar divide is live.
    emit('h00105073); // clear flags so scalar NX and vector NV must both survive
    li(5, 'h40400000); emit('hf0028253); // fmv.w.x f4,x5: scalar denominator 3.0
    vset(2, 31, 3);
    vec('h0b, 8, 8, 8, 0, 0); // vxor.vv v8,v8,v8
    li(5, 'h7f800001); vec(0, 8, 8, 5, 0, 4); // vadd.vx broadcasts raw signaling NaN
    emit('h1840f1d3); // fdiv.s f3,f1,f4,dyn
    vec('h24, 24, 8, 8);
    emit('h00000463); vec(0, 24, 8, 8); // taken BEQ redirects without losing the older authorized FP tail
    vstore(24, 'h2200, 2);
    for (int i = 0; i < 31; i++) expect_store('h2200 + i*4, 'h7fc00000, 2);
    emit('he00181d3); li(10, 'h2280); emit('h00353023); expect_store('h2280, 'h3eaaaaab, 3);
    signature('h001, 'h2288, 17);
    // Empty macro clears vstart without executing or modifying flags.
    vset(2, 0); emit('h0083d073); vec(0, 10, 8, 8);
    signature('h008, 'h20e0, 0); signature('h001, 'h20e8, 17);
    program_words['h700/4] = 'h342021f3; // expose unexpected mcause through the public memory port
    program_words['h704/4] = 'h00303023;
    repeat (4) @(posedge clock);
    @(negedge clock); reset = 0;
  end

  always_comb begin
    instruction_access_in = '0;
    instruction_access_in.request.ready = instruction_access_out.flush || !response_valid;
    instruction_access_in.response.valid = response_valid;
    instruction_access_in.response.bits.word = response_word;
    data_access_in = '0;
    // Reject once, then hold readiness through replay instead of phase-locking
    // a periodic ready waveform against the core's fixed replay latency.
    data_access_in.request.ready = load_delay == 0 && !reject_request;
    data_access_in.response.valid = load_delay == 1;
    data_access_in.response.bits = pending_load;
    data_access_in.drained = load_delay == 0;
  end
  always @(posedge clock) begin
    if (!reset) begin
      cycles <= cycles + 1;
      if (data_access_out.request.valid) reject_request <= data_access_in.request.ready;
      if (load_delay != 0) load_delay <= load_delay - 1;
      if (instruction_access_out.flush || (response_valid && instruction_access_out.response.ready)) response_valid <= 0;
      if (instruction_access_out.request.valid && instruction_access_in.request.ready) begin
        response_valid <= 1;
        assert (instruction_access_out.request.bits.address < 'h800) else $fatal(1, "fetch escaped test ROM");
        response_word <= program_words[instruction_access_out.request.bits.address[10:2]];
      end
      if (data_access_out.request.valid && data_access_in.request.ready) begin
        // The shared LSU returns a tagged completion for stores as well as loads.
        pending_load.access_fault <= 0;
        pending_load.writeback <= data_access_out.request.bits.writeback;
        pending_load.data <= 0;
        load_delay <= 3 + cycles % 4;
        if (data_access_out.request.bits.access == 1) begin
          assert (data_access_out.request.bits.address >= 'h1000 && data_access_out.request.bits.address < 'h2000) else $fatal(1, "unexpected vector load");
          pending_load.data <= memory_words[9'((data_access_out.request.bits.address - 'h1000) >> 3)] >> (8 * (data_access_out.request.bits.address & 7));
        end else begin
          assert (stores < expected_count && data_access_out.request.bits.access == 2) else $fatal(1, "unexpected store");
          assert (data_access_out.request.bits.address == expected_address[stores] && int'(data_access_out.request.bits.width) == expected_width[stores] &&
            (data_access_out.request.bits.data & (expected_width[stores] == 2 ? 64'hffffffff : 64'hffffffffffffffff)) == expected_data[stores])
            else $fatal(1, "signature %0d address %h value %h expected %h", stores, data_access_out.request.bits.address, data_access_out.request.bits.data, expected_data[stores]);
          stores <= stores + 1;
          if (stores + 1 == expected_count) begin
            $display("rv5stage shared scalar/vector FP passed: %0d stores, %0d cycles", expected_count, cycles);
            $finish;
          end
        end
      end
      if (cycles > 15000) $fatal(1, "vector FP timeout stores=%0d fetch=%h", stores, instruction_access_out.request.bits.address);
    end
  end
endmodule
