// Checks shared mul-div, moves, masks, and reductions through architectural memory signatures.
// SPDX-License-Identifier: Apache-2.0
`include "tests/backend/verilog/rv5stage-memory-writeback.svh"
module rv5stage_vector_muldiv_tb;
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
  logic [31:0] program_words [0:16383];
  logic [63:0] memory_words [0:8191];
  logic [63:0] expected_data [0:4095], expected_address [0:4095];
  integer expected_width [0:4095];
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
  task automatic vec(input integer funct6, input integer rd, input integer vs2, input integer vs1, input bit masked = 0, input integer funct3 = 2);
    emit(32'((funct6 << 26) | (int'(!masked) << 25) | (vs2 << 20) | (vs1 << 15) | (funct3 << 12) | (rd << 7) | 'h57));
  endtask
  task automatic vload(input integer rd, input integer address, input integer width);
    li(10, address);
    emit(32'('h02050007 | ((width == 0 ? 0 : width+4) << 12) | (rd << 7)));
  endtask
  task automatic expect_store(input integer address, input logic [63:0] value, input integer width);
    expected_address[expected_count] = 64'(address);
    expected_data[expected_count] = value;
    expected_width[expected_count++] = width;
  endtask
  task automatic vstore(input integer rd, input integer address, input integer width);
    li(10, address);
    emit(32'('h02050027 | ((width == 0 ? 0 : width+4) << 12) | (rd << 7)));
  endtask
  task automatic signature(input integer csr, input integer address, input logic [63:0] value);
    emit(32'((csr << 20) | 'h000021f3)); // csrr x3,csr; all state observers must drain vector work
    li(10, address);
    emit(32'h00353023); // sd x3,0(x10)
    expect_store(address, value, 3);
  endtask

  // Independent integer oracle; no iteration or RTL selection logic is mirrored.
  function automatic logic [63:0] arithmetic(input int code, input int sew, input logic [63:0] left, right);
    logic [63:0] mask, a, b;
    logic signed [127:0] sa, sb, product;
    int width;
    width = 8 << sew; mask = '1 >> (64-width); a = left & mask; b = right & mask;
    sa = $signed({64'b0, a}); sb = $signed({64'b0, b});
    if ((code == 'h21 || code == 'h23 || code == 'h26 || code == 'h27) && a[width-1]) sa -= (128'sd1 << width);
    if ((code == 'h21 || code == 'h23 || code == 'h27) && b[width-1]) sb -= (128'sd1 << width);
    product = sa * sb;
    case (code)
      'h20: return b == 0 ? mask : (a / b);
      'h21: return b == 0 ? mask : (64'(sa / sb) & mask);
      'h22: return b == 0 ? a : (a % b);
      'h23: return b == 0 ? a : (64'(sa % sb) & mask);
      'h25: return 64'(product) & mask;
      default: return 64'(product >> width) & mask;
    endcase
  endfunction
  function automatic logic [63:0] left_value(input int lane, input int sew);
    logic [63:0] sign_bit;
    sign_bit = 64'b1 << ((8 << sew)-1);
    case (lane % 8)
      0: return sign_bit;
      1: return '1;
      2: return sign_bit-1;
      3: return 0;
      4: return 17;
      5: return -64'd29;
      6: return 64'hfedcba9876543210;
      default: return 64'h123456789abcdef;
    endcase
  endfunction
  function automatic logic [63:0] reduce_value(input int op, input int width, input logic [63:0] a, b);
    logic signed [63:0] sa, sb;
    sa = $signed(a << (64-width)) >>> (64-width);
    sb = $signed(b << (64-width)) >>> (64-width);
    case (op)
      0: return (a+b) & ('1 >> (64-width));
      1: return a & b;
      2: return a | b;
      3: return a ^ b;
      4: return a < b ? a : b;
      5: return sa < sb ? a : b;
      6: return a > b ? a : b;
      default: return sa > sb ? a : b;
    endcase
  endfunction
  function automatic logic [63:0] right_value(input int lane);
    case (lane % 8)
      0: return '1; // minimum / -1, plus mixed-sign high multiplication
      1: return 0;  // divide by zero
      2: return 3;
      3: return 0;
      4: return -64'd7;
      5: return 11;
      6: return 64'h8000000080000080;
      default: return '1;
    endcase
  endfunction
  task automatic memory_element(input int address, input logic [63:0] value, input int sew);
    for (int b = 0; b < (1 << sew); b++)
      memory_words[(address - 'h10000 + b) >> 3][8*((address+b)&7) +: 8] = value[8*b +: 8];
  endtask
  task automatic scalar_signature(input int rd, input int address, input logic [63:0] value);
    li(10, address); emit(32'((rd << 20) | 'h53023)); expect_store(address, value, 3);
  endtask

  initial begin
    int address, left_address, right_address, width, code;
    logic [63:0] mask, a, b;
    for (int i = 0; i < 16384; i++) program_words[i] = 'h0000006f;
    for (int i = 0; i < 8192; i++) memory_words[i] = 0;
    li(1, 'hff00); emit('h30509073);
    li(1, 'h200); emit('h30009073); // VS Initial, no scalar FP required
    address = 'h20000;
    for (int sew = 0; sew < 4; sew++) begin
      left_address = 'h10000 + sew*256; right_address = left_address+128;
      width = 8 << sew; mask = '1 >> (64-width);
      for (int lane = 0; lane < 16; lane++) begin
        memory_element(left_address+(lane << sew), left_value(lane,sew), sew);
        memory_element(right_address+(lane << sew), right_value(lane), sew);
      end
      vset(sew, 16, 3);
      vload(8, left_address, sew); vload(16, right_address, sew);
      for (int op = 0; op < 8; op++) begin
        code = 'h20+op;
        for (int vx = 0; vx < 2; vx++) begin
          li(5, -3); li(6, 7);
          vec(code, 24, 8, vx != 0 ? 5 : 16, 0, vx != 0 ? 6 : 2);
          // Younger scalar operations contend with the accepted vector tail.
          // Changing x5 also checks that VX captured its original scalar operand.
          li(5, 9);
          emit('h026283b3); // mul x7,x5,x6 = 63
          emit('h0262ceb3); // div x29,x5,x6 = 1
          emit('h00000463); emit('h027283b3); // taken branch kills younger multiply
          scalar_signature(7, address, 63); address += 8;
          scalar_signature(29, address, 1); address += 8;
          vstore(24, address, sew);
          for (int lane = 0; lane < 16; lane++)
            expect_store(address+(lane << sew), arithmetic(code,sew,left_value(lane,sew),vx != 0 ? -64'd3 : right_value(lane)),sew);
          address += 128;
        end
      end
      // In-place, masked, restarted operations preserve disabled and prestart lanes.
      emit('h00000463); vec('h25, 8, 8, 16, 0, 2); // older branch squashes vector execution before WB
      vec('h1f, 0, 8, 0, 0, 3); // vmsgt.vi v0,v8,0
      emit('h0081d073); // vstart=3
      vec('h26, 8, 8, 16, 1, 2);
      vstore(8, address, sew);
      for (int lane = 0; lane < 16; lane++) begin
        a = left_value(lane,sew) & mask;
        b = right_value(lane) & mask;
        expect_store(address+(lane << sew), lane >= 3 && a != 0 && !a[width-1] ? arithmetic('h26,sew,a,b) : a,sew);
      end
      address += 128;
      signature('h008,address,0); address += 8;
      // An all-masked operation still drains its ordered completion slots.
      vec('h19, 0, 8, 8, 0, 0);
      vec('h21, 8, 8, 16, 1, 2);
      vstore(8, address, sew);
      for (int lane = 0; lane < 16; lane++) begin
        a = left_value(lane,sew) & mask;
        b = right_value(lane) & mask;
        expect_store(address+(lane << sew), lane >= 3 && a != 0 && !a[width-1] ? arithmetic('h26,sew,a,b) : a,sew);
      end
      address += 128;
      // VL=0 plus nonzero vstart emits exactly one empty macro completion.
      vset(sew,0,3); emit('h0083d073); vec('h27, 24, 8, 16, 0, 2);
      signature('h008,address,0); address += 8;
    end
    // Exercise the new cheap operations through Decode and real WB, not just
    // the standalone unroller. Older branches must squash each new family.
    for (int sew = 0; sew < 4; sew++) begin
      width = 8 << sew; mask = '1 >> (64-width);
      vset(sew,16,3);
      vload(8,'h10000+sew*256,sew); vload(16,'h10080+sew*256,sew);
      for (int form = 0; form < 3; form++) begin
        li(5,-7);
        vec('h17,24,0,form == 0 ? 16 : form == 1 ? 5 : 29,0,form == 0 ? 0 : form == 1 ? 4 : 3);
        li(5,13); // the broadcast must have captured -7, not this value
        emit('h00000463); vec('h17,24,0,0,0,3);
        vstore(24,address,sew);
        for (int lane = 0; lane < 16; lane++)
          expect_store(address+(lane << sew),(form == 0 ? right_value(lane) : form == 1 ? -64'd7 : -64'd3) & mask,sew);
        address += 128;
        // Selection is data: the zero half of the mask still writes vs2.
        vec('h1f,0,8,0,0,3);
        emit('h0081d073);
        li(5,-9);
        vec('h17,24,8,form == 0 ? 16 : form == 1 ? 5 : 17,1,form == 0 ? 0 : form == 1 ? 4 : 3);
        emit('h00000463); vec('h17,24,16,0,1,3);
        vstore(24,address,sew);
        for (int lane = 0; lane < 16; lane++) begin
          a = left_value(lane,sew) & mask;
          b = lane < 3 ? (form == 0 ? right_value(lane) : form == 1 ? -64'd7 : -64'd3) :
              a != 0 && !a[width-1] ? (form == 0 ? right_value(lane) : form == 1 ? -64'd9 : -64'd15) : a;
          expect_store(address+(lane << sew),b & mask,sew);
        end
        address += 128;
      end
    end
    // Mask logic uses 128 one-bit elements even though its registers are
    // unaligned for the current LMUL=8. Read back both physical mask words.
    for (int op = 24; op < 32; op++) begin
      logic [63:0] expected;
      vset(3,2);
      vload(3,'h10000,3); vload(5,'h10080,3); vload(7,'h10100,3);
      li(1,128); emit('h0030f057); // vsetvli x0,x1,e8,m8
      li(1,63); emit('h00809073); // vstart straddles mask words
      vec(op,3,3,5,0,2);
      emit('h00000463); vec(27,3,3,3,0,2);
      vset(3,2); vstore(3,address,3);
      for (int word_index = 0; word_index < 2; word_index++) begin
        a = memory_words[word_index]; b = memory_words[16+word_index];
        case (op)
          24: expected = a & ~b;
          25: expected = a & b;
          26: expected = a | b;
          27: expected = a ^ b;
          28: expected = a | ~b;
          29: expected = ~(a & b);
          30: expected = ~(a | b);
          default: expected = ~(a ^ b);
        endcase
        if (word_index == 0) expected = {expected[63],a[62:0]};
        expect_store(address+word_index*8,expected,3);
      end
      address += 16;
      signature('h008,address,0); address += 8;
    end
    for (int sew=0;sew<4;sew++) begin
      logic [63:0] accumulator, scalar_value;
      width=8<<sew; mask='1>>(64-width);
      for (int op=0;op<8;op++) begin
        vset(sew,2); vload(3,'h10080+sew*256,sew); vload(7,'h10080+sew*256,sew);
        vset(sew,16,3); vload(8,'h10000+sew*256,sew);
        vec('h1f,0,8,0,0,3); // positive source elements define v0
        accumulator=mask;
        for (int i=0;i<16;i++) begin
          a=left_value(i,sew)&mask;
          if (op%2==0 || (a!=0 && !a[width-1])) accumulator=reduce_value(op,width,accumulator,a);
        end
        vec(op,7,8,3,op%2!=0,2);
        vec(16,5,7,0,0,2); // vmv.x.s x5,v7, with an immediate dependent
        emit('h00128313); // addi x6,x5,1
        scalar_value=64'($signed(accumulator<<(64-width)) >>> (64-width));
        scalar_signature(6,address,scalar_value+1); address+=8;
        vset(sew,2); vstore(7,address,sew);
        expect_store(address,accumulator,sew); expect_store(address+(1<<sew),0,sew); address+=16;
        signature('h008,address,0); address+=8;
      end
      // Moves ignore LMUL alignment, and only extraction ignores empty bodies.
      vset(sew,2); vload(7,'h10080+sew*256,sew);
      vset(sew,16,3); li(5,-9); vec(16,7,0,5,0,6); // vmv.s.x
      emit('h00000463); vec(16,7,0,0,0,6); // squashed insertion
      vset(sew,0,3); vec(16,7,0,0,0,6); // empty insertion
      emit('h0083d073); vec(16,5,7,0,0,2); // extraction with vl=0,vstart=7
      emit('h00128313); scalar_signature(6,address,-64'd8); address+=8;
      signature('h008,address,0); address+=8;
      vset(sew,2); vstore(7,address,sew);
      expect_store(address,-64'd9&mask,sew); expect_store(address+(1<<sew),0,sew); address+=16;
      vset(sew,0,3); vec(0,7,8,3,0,2); // empty reduction must not copy seed
      vec(16,5,7,0,0,2); scalar_signature(5,address,-64'd9); address+=8;
      vec(16,0,7,0,0,2); scalar_signature(0,address,0); address+=8;
      li(5,7); emit('h00000463); vec(16,5,7,0,0,2);
      scalar_signature(5,address,7); address+=8;
      // An older deferred GPR writer must drain before vmv.x.s overwrites it.
      li(5,7); li(6,13); emit('h026282b3); vec(16,5,7,0,0,2);
      scalar_signature(5,address,-64'd9); address+=8;
    end
    // Nonzero vstart is illegal for every reduction, even with VL=0. The trap
    // handler skips the instruction; a sentinel catches unintended VRF writes.
    for (int op=0;op<8;op++) begin
      vset(0,op%2==0 ? 8 : 0); emit('h0080d073);
      vec(op,7,8,3,0,2); expect_store('h2fff0,2,3);
      vec(16,5,7,0,0,2); scalar_signature(5,address,-64'd9); address+=8;
    end
    assert(pc < 'hff00/4) else $fatal(1,"program exceeds ROM");
    pc='hff00/4;
    emit('h342021f3); li(10,'h2fff0); emit('h00353023);
    emit('h341021f3); emit('h00418193); emit('h34119073);
    emit('h00801073); emit('h30200073);
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
        assert (instruction_access_out.request.bits.address < 'h10000) else $fatal(1, "fetch escaped test ROM");
        response_word <= program_words[instruction_access_out.request.bits.address[15:2]];
      end
      if (data_access_out.request.valid && data_access_in.request.ready) begin
        // The shared LSU returns a tagged completion for stores as well as loads.
        pending_load.access_fault <= 0;
        pending_load.writeback <= data_access_out.request.bits.writeback;
        pending_load.data <= 0;
        load_delay <= 3 + cycles % 4;
        if (data_access_out.request.bits.access == 1) begin
          assert (data_access_out.request.bits.address >= 'h10000 && data_access_out.request.bits.address < 'h20000) else $fatal(1, "unexpected vector load");
          pending_load.data <= memory_words[13'((data_access_out.request.bits.address - 'h10000) >> 3)] >> (8 * (data_access_out.request.bits.address & 7));
        end else begin
          assert (stores < expected_count && data_access_out.request.bits.access == 2) else $fatal(1, "unexpected store");
          assert (data_access_out.request.bits.address == expected_address[stores] && int'(data_access_out.request.bits.width) == expected_width[stores] &&
            (data_access_out.request.bits.data & (64'hffffffffffffffff >> (64-(8 << expected_width[stores])))) == expected_data[stores])
            else $fatal(1, "signature %0d address %h value %h expected %h", stores, data_access_out.request.bits.address, data_access_out.request.bits.data, expected_data[stores]);
          stores <= stores + 1;
          if (stores + 1 == expected_count) begin
            $display("rv5stage shared muldiv, vector moves/masks/reductions passed: %0d stores, %0d cycles", expected_count, cycles);
            $finish;
          end
        end
      end
      if (cycles > 300000) $fatal(1, "vector muldiv timeout stores=%0d fetch=%h", stores, instruction_access_out.request.bits.address);
    end
  end
endmodule
