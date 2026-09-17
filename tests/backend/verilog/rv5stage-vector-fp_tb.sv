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
  logic [31:0] program_words [0:1023];
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
  task automatic widening_conversion(input integer selector, rd, vs2, address, input logic [63:0] first, second);
    vset(2, 2); vec('h12, rd, vs2, selector);
    vset(3, 2, 1); vstore(rd, address, 3);
    expect_store(address, first, 3); expect_store(address + 8, second, 3);
  endtask
  task automatic narrowing_conversion(input integer selector, rd, vs2, address, input logic [31:0] first, second);
    vset(2, 2); vec('h12, rd, vs2, selector);
    vstore(rd, address, 2);
    expect_store(address, 64'(first), 2); expect_store(address + 4, 64'(second), 2);
  endtask

  initial begin
    for (int i = 0; i < 1024; i++) program_words[i] = 'h0000006f;
    for (int i = 0; i < 512; i++) memory_words[i] = 0;
    li(1, 'hf00); emit('h30509073); // trap vector: timeout reports a precise unexpected fault
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
    // The same shared service handles variable-latency, sign/minmax, fused,
    // and mask-producing vector-vector operations in ordered completion slots.
    vec('h20, 13, 8, 9); vstore(13, 'h2300, 2);
    expect_store('h2300, 'h40000000, 2); expect_store('h2304, 64'hc0000000, 2);
    expect_store('h2308, 'h3fc00000, 2); expect_store('h230c, 64'hc0000000, 2);
    memory_words[18] = 'h408000003f800000; memory_words[19] = 'h4180000041100000;
    vload(14, 'h1090, 2); vec('h13, 15, 14, 0); vstore(15, 'h2310, 2);
    expect_store('h2310, 'h3f800000, 2); expect_store('h2314, 'h40000000, 2);
    expect_store('h2318, 'h40400000, 2); expect_store('h231c, 'h40800000, 2);
    vec('h09, 16, 8, 9); vec('h04, 17, 8, 9); vec('h06, 18, 8, 9);
    vstore(16, 'h2320, 2);
    expect_store('h2320, 64'hbf800000, 2); expect_store('h2324, 'h40000000, 2);
    expect_store('h2328, 64'hc0400000, 2); expect_store('h232c, 'h40800000, 2);
    vstore(17, 'h2330, 2);
    expect_store('h2330, 'h3f000000, 2); expect_store('h2334, 64'hbf800000, 2);
    expect_store('h2338, 'h40000000, 2); expect_store('h233c, 64'hc0000000, 2);
    vstore(18, 'h2340, 2);
    expect_store('h2340, 'h3f800000, 2); expect_store('h2344, 'h40000000, 2);
    expect_store('h2348, 'h40400000, 2); expect_store('h234c, 'h40800000, 2);
    vec('h17, 19, 0, 10, 0, 0); vec('h2c, 19, 8, 9); vstore(19, 'h2350, 2);
    expect_store('h2350, 'h40000000, 2); expect_store('h2354, 64'hbf800000, 2);
    expect_store('h2358, 'h41300000, 2); expect_store('h235c, 64'hc0c00000, 2);
    vec('h17, 20, 0, 11, 0, 0); vec('h28, 20, 8, 9); vstore(20, 'h2360, 2);
    expect_store('h2360, 'h3fa00000, 2); expect_store('h2364, 64'hbf800000, 2);
    expect_store('h2368, 'h40a00000, 2); expect_store('h236c, 64'hc1000000, 2);
    memory_words[20] = 'hbf8000003f800000; memory_words[21] = 'h4080000000000000;
    vload(21, 'h10a0, 2); vec('h18, 0, 8, 21); // vmfeq.vv selects lanes 0 and 3
    vec('h17, 22, 0, 8, 0, 0); vec(0, 22, 8, 9, 1); vstore(22, 'h2370, 2);
    expect_store('h2370, 'h3fc00000, 2); expect_store('h2374, 'h40000000, 2);
    expect_store('h2378, 'h40400000, 2); expect_store('h237c, 'h40000000, 2);
    // Vector-scalar FP snapshots the forwarded architectural FPR at WB, then
    // broadcasts it without consuming a general VRF read port.
    li(5, 'h3f800000); emit('hf0028453); // fmv.w.x f8,x5
    vec(0, 23, 8, 8, 0, 5);
    li(5, 'h40000000); emit('hf0028453); // younger overwrite must not change the admitted macro
    vec('h27, 24, 8, 1, 0, 5);
    emit('h0010f2d3); // fadd.s f5,f1,f1,dyn; the adjacent .vf must wait and forward
    vec(0, 25, 8, 5, 0, 5);
    vec('h17, 26, 0, 8, 0, 0); vec('h2c, 26, 8, 1, 0, 5);
    memory_words[23] = 'h000000003f800000;
    li(10, 'h10b8); emit('h00053307); // fld f6,0(x10): invalid FP32 NaN box
    vec(0, 27, 8, 6, 0, 5);
    vec('h1d, 0, 8, 1, 0, 5); // vmfgt.vf: lanes 1..3
    vec('h17, 28, 0, 8, 0, 0); emit('h00815073); // csrwi vstart,2
    vec(0, 28, 8, 1, 1, 5);
    vstore(23, 'h2380, 2);
    expect_store('h2380, 'h40000000, 2); expect_store('h2384, 'h40400000, 2);
    expect_store('h2388, 'h40800000, 2); expect_store('h238c, 'h40a00000, 2);
    vstore(24, 'h2390, 2);
    expect_store('h2390, 'h00000000, 2); expect_store('h2394, 64'hbf800000, 2);
    expect_store('h2398, 64'hc0000000, 2); expect_store('h239c, 64'hc0400000, 2);
    vstore(25, 'h23a0, 2);
    expect_store('h23a0, 'h40400000, 2); expect_store('h23a4, 'h40800000, 2);
    expect_store('h23a8, 'h40a00000, 2); expect_store('h23ac, 'h40c00000, 2);
    vstore(26, 'h23b0, 2);
    expect_store('h23b0, 'h40000000, 2); expect_store('h23b4, 'h40800000, 2);
    expect_store('h23b8, 'h40c00000, 2); expect_store('h23bc, 'h41000000, 2);
    vstore(27, 'h23c0, 2);
    for (int i = 0; i < 4; i++) expect_store('h23c0 + i*4, 'h7fc00000, 2);
    vstore(28, 'h23d0, 2);
    expect_store('h23d0, 'h3f800000, 2); expect_store('h23d4, 'h40000000, 2);
    expect_store('h23d8, 'h40800000, 2); expect_store('h23dc, 'h40a00000, 2);
    // Same-width conversions choose integer or FP completion data explicitly;
    // fixed-RTZ ignores frm while ordinary conversion uses dynamic RNE.
    emit('h00105073); // clear fflags
    vec('h12, 29, 10, 1); vec('h12, 30, 10, 7);
    vstore(29, 'h2400, 2);
    expect_store('h2400, 2, 2); expect_store('h2404, 1, 2);
    expect_store('h2408, 5, 2); expect_store('h240c, 2, 2);
    vstore(30, 'h2410, 2);
    expect_store('h2410, 1, 2); expect_store('h2414, 1, 2);
    expect_store('h2418, 5, 2); expect_store('h241c, 2, 2);
    memory_words[25] = 'hfffffffe00000001; memory_words[26] = 'hfffffffc00000003;
    vload(31, 'h10c8, 2); vec('h12, 29, 31, 3); vstore(29, 'h2420, 2);
    expect_store('h2420, 'h3f800000, 2); expect_store('h2424, 64'hc0000000, 2);
    expect_store('h2428, 'h40400000, 2); expect_store('h242c, 64'hc0800000, 2);
    vec('h12, 30, 31, 2); vstore(30, 'h2470, 2);
    expect_store('h2470, 'h3f800000, 2); expect_store('h2474, 'h4f800000, 2);
    expect_store('h2478, 'h40400000, 2); expect_store('h247c, 'h4f800000, 2);
    vec('h12, 31, 9, 0); vstore(31, 'h2480, 2);
    expect_store('h2480, 0, 2); expect_store('h2484, 0, 2);
    expect_store('h2488, 2, 2); expect_store('h248c, 0, 2);
    signature('h001, 'h2490, 17); emit('h00105073);
    // Widening conversions read E32/LMUL1 and write E64/LMUL2. Every result
    // domain and fixed/dynamic rounding form is observed through vector stores.
    memory_words[29] = 'hc03000003fc00000; // 1.5,-2.75
    memory_words[30] = 'hfffffffc00000003; // 3,-4
    memory_words[31] = 'h00000005ffffffff; // 2^32-1,5
    vset(2, 2); vload(8, 'h10e8, 2); vload(9, 'h10f0, 2); vload(10, 'h10f8, 2);
    widening_conversion(8, 16, 8, 'h2500, 2, 0);
    widening_conversion(9, 18, 8, 'h2510, 2, -3);
    widening_conversion(10, 20, 10, 'h2520, 64'h41efffffffe00000, 64'h4014000000000000);
    widening_conversion(11, 22, 9, 'h2530, 64'h4008000000000000, 64'hc010000000000000);
    widening_conversion(12, 24, 8, 'h2540, 64'h3ff8000000000000, 64'hc006000000000000);
    widening_conversion(14, 26, 8, 'h2550, 1, 0);
    widening_conversion(15, 28, 8, 'h2560, 1, -2);
    // Widening arithmetic exactly promotes each E32 operand at the shared FP
    // boundary. The .w forms retain a wide vs2, and fused forms retain wide vd.
    memory_words[45] = 'hc00000003fc00000; // 1.5,-2
    memory_words[46] = 'h4080000040000000; // 2,4
    memory_words[47] = 'h4024000000000000; memory_words[48] = 'hc034000000000000; // 10,-20
    memory_words[49] = 'h3ff0000000000000; memory_words[50] = 'h4000000000000000; // 1,2
    vset(2, 2); vload(8, 'h1168, 2); vload(9, 'h1170, 2);
    li(5, 'h3f000000); emit('hf0028453); // fmv.w.x f8,x5: 0.5
    vec('h30, 18, 8, 9); vset(3, 2, 1); vstore(18, 'h2630, 3);
    expect_store('h2630, 'h400c000000000000, 3); expect_store('h2638, 'h4000000000000000, 3);
    vset(2, 2); vec('h30, 20, 8, 8, 0, 5); vset(3, 2, 1); vstore(20, 'h2640, 3);
    expect_store('h2640, 'h4000000000000000, 3); expect_store('h2648, 'hbff8000000000000, 3);
    vload(16, 'h1178, 3); vset(2, 2); vec('h34, 16, 16, 9); vset(3, 2, 1); vstore(16, 'h2650, 3);
    expect_store('h2650, 'h4028000000000000, 3); expect_store('h2658, 'hc030000000000000, 3);
    vload(16, 'h1178, 3); vset(2, 2); vec('h36, 16, 16, 8, 0, 5); vset(3, 2, 1); vstore(16, 'h2660, 3);
    expect_store('h2660, 'h4023000000000000, 3); expect_store('h2668, 'hc034800000000000, 3);
    vset(2, 2); vec('h38, 22, 8, 9); vec('h38, 24, 8, 8, 0, 5);
    vset(3, 2, 1); vstore(22, 'h2670, 3); vstore(24, 'h2680, 3);
    expect_store('h2670, 'h4008000000000000, 3); expect_store('h2678, 'hc020000000000000, 3);
    expect_store('h2680, 'h3fe8000000000000, 3); expect_store('h2688, 'hbff0000000000000, 3);
    vload(30, 'h1188, 3); vset(2, 2); vec('h3c, 30, 8, 9); vset(3, 2, 1); vstore(30, 'h2690, 3);
    expect_store('h2690, 'h4010000000000000, 3); expect_store('h2698, 'hc018000000000000, 3);
    vload(30, 'h1188, 3); vset(2, 2); vec('h3d, 30, 8, 8, 0, 5); vset(3, 2, 1); vstore(30, 'h26a0, 3);
    expect_store('h26a0, 'hbffc000000000000, 3); expect_store('h26a8, 'hbff0000000000000, 3);
    vload(30, 'h1188, 3); vset(2, 2); vec('h3e, 30, 8, 9); vset(3, 2, 1); vstore(30, 'h26b0, 3);
    expect_store('h26b0, 'h4000000000000000, 3); expect_store('h26b8, 'hc024000000000000, 3);
    vload(30, 'h1188, 3); vset(2, 2); vec('h3f, 30, 8, 8, 0, 5); vset(3, 2, 1); vstore(30, 'h26c0, 3);
    expect_store('h26c0, 'h3fd0000000000000, 3); expect_store('h26c8, 'h4008000000000000, 3);
    // A signaling narrow input raises NV during exact widening even though the
    // wide arithmetic lane subsequently receives a quiet NaN.
    emit('h00105073); memory_words[51] = 'h3f8000007f800001;
    vset(2, 2); vload(8, 'h1198, 2); vec('h30, 18, 8, 9);
    vset(3, 2, 1); vstore(18, 'h26d0, 3);
    expect_store('h26d0, 'h7ff8000000000000, 3); expect_store('h26d8, 'h4014000000000000, 3);
    signature('h001, 'h26e0, 16); emit('h00105073);
    // Floating-point reductions serialize active elements through the shared
    // service, retain only their accumulator, and write vector element zero.
    memory_words[52] = 'h400000003f800000; memory_words[53] = 'h4080000040400000; // 1,2,3,4
    memory_words[54] = 'h0000000041200000; // FP32 seed 10
    memory_words[55] = 'h4024000000000000; // FP64 seed 10
    vset(2, 4); vload(8, 'h11a0, 2); vload(9, 'h11b0, 2);
    vec('h01, 10, 8, 9); vec('h03, 11, 8, 9); vec('h05, 12, 8, 9); vec('h07, 13, 8, 9);
    vset(2, 1); vstore(10, 'h26f0, 2); vstore(11, 'h26f8, 2); vstore(12, 'h2700, 2); vstore(13, 'h2708, 2);
    expect_store('h26f0, 'h41a00000, 2); expect_store('h26f8, 'h41a00000, 2);
    expect_store('h2700, 'h3f800000, 2); expect_store('h2708, 'h41200000, 2);
    vset(3, 1); vload(14, 'h11b8, 3); vset(2, 4);
    vec('h31, 16, 8, 14); vec('h33, 18, 8, 14);
    vset(3, 1); vstore(16, 'h2710, 3); vstore(18, 'h2718, 3);
    expect_store('h2710, 'h4034000000000000, 3); expect_store('h2718, 'h4034000000000000, 3);
    // With no active elements the seed is copied exactly and no exception is
    // raised, even when the source and seed contain signaling NaNs.
    emit('h00105073); memory_words[56] = 'h7f8000017f800001; memory_words[57] = 'h000000007f800001;
    vset(2, 4); vload(20, 'h11c0, 2); vload(21, 'h11c8, 2);
    vec('h19, 0, 20, 20, 0, 0); // vmsne.vv v0,v20,v20: all disabled
    vec('h03, 22, 20, 21, 1); vset(2, 1); vstore(22, 'h2720, 2);
    expect_store('h2720, 'h7f800001, 2); signature('h001, 'h2728, 0);
    // An active signaling NaN in a non-final fold contributes NV and yields
    // the canonical NaN only when the ordered reduction reaches its final beat.
    emit('h00105073); memory_words[58] = 'h7f8000013f800000; memory_words[59] = 'h4040000040000000;
    memory_words[60] = 0; vset(2, 4); vload(24, 'h11d0, 2); vload(25, 'h11e0, 2);
    vec('h03, 26, 24, 25); vset(2, 1); vstore(26, 'h2730, 2);
    expect_store('h2730, 'h7fc00000, 2); signature('h001, 'h2738, 16); emit('h00105073);
    // VL=0 performs no fold and leaves the destination untouched.
    memory_words[61] = 'h0000000040e00000; vset(2, 1); vload(28, 'h11e8, 2);
    vset(2, 0); vec('h01, 28, 8, 9); vset(2, 1); vstore(28, 'h2740, 2);
    expect_store('h2740, 'h40e00000, 2); signature('h001, 'h2748, 0);
    // Narrowing applies doubled EMUL to its E64 source and writes E32/LMUL1.
    memory_words[32] = 'h3ff8000000000000; memory_words[33] = 'hc006000000000000;
    memory_words[34] = 64'd16777217; memory_words[35] = 64'h00000000ffffffff;
    memory_words[36] = 3; memory_words[37] = -4;
    memory_words[38] = 'h3ff0000010000000; memory_words[39] = 'hbff0000010000000;
    vset(3, 2, 1); vload(8, 'h1100, 3); vload(10, 'h1110, 3); vload(12, 'h1120, 3); vload(14, 'h1130, 3);
    narrowing_conversion(16, 16, 8, 'h2580, 2, 0);
    narrowing_conversion(17, 17, 8, 'h2590, 2, -3);
    narrowing_conversion(18, 18, 10, 'h25a0, 32'h4b800000, 32'h4f800000);
    narrowing_conversion(19, 19, 12, 'h25b0, 32'h40400000, 32'hc0800000);
    narrowing_conversion(20, 20, 8, 'h25c0, 32'h3fc00000, 32'hc0300000);
    narrowing_conversion(21, 21, 14, 'h25d0, 32'h3f800001, 32'hbf800001);
    narrowing_conversion(22, 22, 8, 'h25e0, 1, 0);
    narrowing_conversion(23, 23, 8, 'h25f0, 1, -2);
    signature('h001, 'h2600, 17); emit('h00105073);
    // A restarted, masked widening singleton preserves distinct pre-vstart and
    // masked-off E64 destinations while converting the later enabled element.
    memory_words[40] = 1; memory_words[41] = 1; // E32 mask source: 1,0,1
    memory_words[42] = 11; memory_words[43] = 22; memory_words[44] = 33;
    vset(3, 3, 1); vload(30, 'h1150, 3);
    vset(2, 3); vload(8, 'h1000, 2); vload(29, 'h1140, 2);
    vec('h1f, 0, 29, 0, 0, 3); emit('h0080d073); vec('h12, 30, 8, 12, 1);
    vset(3, 3, 1); vstore(30, 'h2610, 3);
    expect_store('h2610, 11, 3); expect_store('h2618, 22, 3); expect_store('h2620, 64'h4008000000000000, 3);
    // In-place singleton writes must preserve the other FP32 half and pre-vstart lane.
    vset(2, 4); vload(8, 'h1000, 2); vload(9, 'h1010, 2); emit('h0080d073); // csrwi vstart,1
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
    memory_words[24] = 'h3fe0000000000000;
    li(10, 'h10c0); emit('h00053387); // fld f7,0(x10)
    vec(0, 13, 8, 7, 0, 5); vstore(13, 'h23e0, 3);
    expect_store('h23e0, 'h3ff8000000000000, 3); expect_store('h23e8, 'h4004000000000000, 3);
    vec('h12, 14, 9, 1); vstore(14, 'h2440, 3);
    expect_store('h2440, 0, 3); expect_store('h2448, 64'hffffffffffffffff, 3);
    memory_words[27] = 1; memory_words[28] = 64'hfffffffffffffffe;
    vload(15, 'h10d8, 3); vec('h12, 16, 15, 3); vstore(16, 'h2450, 3);
    expect_store('h2450, 'h3ff0000000000000, 3); expect_store('h2458, 'hc000000000000000, 3);
    signature('h001, 'h2460, 1); emit('h00105073);
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
    // FP movement stays on the packed merge/slide datapath. Scalar sources
    // are NaN-box checked, while vector-to-FPR E32 results are NaN-boxed at WB.
    memory_words[62] = 'h400000003f800000; memory_words[63] = 'h4080000040400000;
    memory_words[65] = 'h0000000200000001; memory_words[66] = 'h0000000400000003;
    vset(2, 4); vload(8, 'h11f0, 2); vload(19, 'h1208, 2);
    li(5, 'h3f000000); emit('hf0028453); // fmv.w.x f8,x5: 0.5
    vec('h17, 10, 0, 8, 0, 5); // vfmv.v.f
    vec('h1f, 0, 19, 2, 0, 3); // vmsgt.vi: lanes 2 and 3
    vec('h17, 11, 8, 8, 1, 5); // vfmerge.vfm
    vec('h0e, 12, 8, 8, 0, 5); // vfslide1up.vf
    vec('h0f, 13, 8, 8, 0, 5); // vfslide1down.vf
    vload(14, 'h11f0, 2); vec('h10, 14, 0, 8, 0, 5); // vfmv.s.f
    vstore(10, 'h2750, 2); vstore(11, 'h2760, 2); vstore(12, 'h2770, 2);
    vstore(13, 'h2780, 2); vstore(14, 'h2790, 2);
    for (int i = 0; i < 4; i++) expect_store('h2750 + i*4, 'h3f000000, 2);
    expect_store('h2760, 'h3f800000, 2); expect_store('h2764, 'h40000000, 2);
    expect_store('h2768, 'h3f000000, 2); expect_store('h276c, 'h3f000000, 2);
    expect_store('h2770, 'h3f000000, 2); expect_store('h2774, 'h3f800000, 2);
    expect_store('h2778, 'h40000000, 2); expect_store('h277c, 'h40400000, 2);
    expect_store('h2780, 'h40000000, 2); expect_store('h2784, 'h40400000, 2);
    expect_store('h2788, 'h40800000, 2); expect_store('h278c, 'h3f000000, 2);
    expect_store('h2790, 'h3f000000, 2); expect_store('h2794, 'h40000000, 2);
    expect_store('h2798, 'h40400000, 2); expect_store('h279c, 'h40800000, 2);
    // vfmv.f.s is independent of vl and immediately feeds a younger scalar
    // observer only after its WB-owned FPR reservation has completed.
    vset(2, 0); vec('h10, 9, 8, 0, 0, 1); // vfmv.f.s f9,v8
    emit(32'('he2000053 | (9 << 15) | (3 << 7))); // fmv.x.d x3,f9
    li(10, 'h27a0); emit('h00353023); expect_store('h27a0, 'hffffffff3f800000, 3);
    emit('h0080006f); vec('h10, 9, 9, 0, 0, 1); // squashed vector-to-FPR write
    emit(32'('he2000053 | (9 << 15) | (3 << 7)));
    li(10, 'h27a8); emit('h00353023); expect_store('h27a8, 'hffffffff3f800000, 3);
    memory_words[64] = 'h000000007f800001; vset(2, 1); vload(18, 'h1200, 2);
    vset(2, 0); vec('h10, 10, 18, 0, 0, 1); // a moved signaling-NaN payload is not canonicalized
    emit(32'('he2000053 | (10 << 15) | (3 << 7)));
    li(10, 'h27e0); emit('h00353023); expect_store('h27e0, 'hffffffff7f800001, 3);
    li(10, 'h10b8); emit('h00053307); // fld f6,0(x10): invalid FP32 NaN box
    vset(2, 4); vec('h17, 18, 0, 6, 0, 5); vstore(18, 'h27f0, 2);
    for (int i = 0; i < 4; i++) expect_store('h27f0 + i*4, 'h7fc00000, 2);
    // Vector destinations are unchanged at vl=0 for insertion and FP slides.
    vset(2, 4); vload(15, 'h11f0, 2); vload(16, 'h11f0, 2); vload(17, 'h11f0, 2);
    vset(2, 0); vec('h10, 15, 0, 8, 0, 5); vec('h0e, 16, 8, 8, 0, 5); vec('h0f, 17, 8, 8, 0, 5);
    vset(2, 4); vstore(15, 'h27b0, 2); vstore(16, 'h27c0, 2); vstore(17, 'h27d0, 2);
    for (int destination = 0; destination < 3; destination++) begin
      expect_store('h27b0 + destination*16, 'h3f800000, 2);
      expect_store('h27b4 + destination*16, 'h40000000, 2);
      expect_store('h27b8 + destination*16, 'h40400000, 2);
      expect_store('h27bc + destination*16, 'h40800000, 2);
    end
    // Empty macro clears vstart without executing or modifying flags.
    vset(2, 0); emit('h0083d073); vec(0, 10, 8, 8);
    signature('h008, 'h20e0, 0); signature('h001, 'h20e8, 17);
    program_words['hf00/4] = 'h342021f3; // expose unexpected mcause through the public memory port
    program_words['hf04/4] = 'h00303023;
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
        assert (instruction_access_out.request.bits.address < 'h1000) else $fatal(1, "fetch escaped test ROM");
        response_word <= program_words[instruction_access_out.request.bits.address[11:2]];
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
