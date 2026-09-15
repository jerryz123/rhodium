// Checks shared vector integer, fixed-point multiply, mul-div, move, mask, and permutation paths through memory signatures.
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
  logic [63:0] expected_data [0:8191], expected_address [0:8191];
  integer expected_width [0:8191];
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
  task automatic write_vector_csr(input integer csr, input integer value);
    emit(32'((csr << 20) | (value << 15) | 'h5073));
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
  function automatic logic [63:0] fractional_multiply(input int sew, mode, input logic [63:0] left, right);
    logic [63:0] mask, a, b;
    logic signed [127:0] sa, sb, product, shifted;
    logic [127:0] discarded_mask;
    logic round_bit, lower_nonzero, discarded_nonzero, increment;
    int width, distance;
    width = 8 << sew; distance = width - 1; mask = '1 >> (64-width); a = left & mask; b = right & mask;
    sa = $signed({64'b0, a}); sb = $signed({64'b0, b});
    if (a[width-1]) sa -= 128'sd1 << width;
    if (b[width-1]) sb -= 128'sd1 << width;
    product = sa * sb; shifted = product >>> distance;
    round_bit = product[distance-1];
    discarded_mask = (128'd1 << distance) - 1;
    lower_nonzero = (product & (discarded_mask >> 1)) != 0;
    discarded_nonzero = (product & discarded_mask) != 0;
    case (mode)
      0: increment = round_bit;
      1: increment = round_bit && (lower_nonzero || shifted[0]);
      2: increment = 0;
      3: increment = !shifted[0] && discarded_nonzero;
    endcase
    if (a == (64'd1 << (width-1)) && b == (64'd1 << (width-1))) return mask >> 1;
    return 64'(shifted + 128'(increment)) & mask;
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
  function automatic logic [63:0] widening_value(input int op, input int width, input logic [63:0] a, b);
    logic [63:0] source_mask, left_mask, destination_mask;
    logic signed [63:0] signed_a, signed_b;
    source_mask = '1 >> (64-width); destination_mask = '1 >> (64-2*width);
    left_mask = op >= 'h34 ? destination_mask : source_mask;
    signed_a = $signed((a & left_mask) << (64-(op >= 'h34 ? 2*width : width))) >>> (64-(op >= 'h34 ? 2*width : width));
    signed_b = $signed((b & source_mask) << (64-width)) >>> (64-width);
    if (op[0]) return (op[1] ? signed_a-signed_b : signed_a+signed_b) & destination_mask;
    return (op[1] ? (a & left_mask)-(b & source_mask) : (a & left_mask)+(b & source_mask)) & destination_mask;
  endfunction
  function automatic logic [63:0] widening_multiply_value(input int op, input int width, input logic [63:0] a, b);
    logic [63:0] source_mask, destination_mask;
    logic signed [127:0] signed_a, signed_b, product;
    source_mask = '1 >> (64-width); destination_mask = '1 >> (64-2*width);
    signed_a = $signed({64'b0,a & source_mask}); signed_b = $signed({64'b0,b & source_mask});
    if (op != 'h38 && a[width-1]) signed_a -= 128'sd1 << width;
    if (op == 'h3b && b[width-1]) signed_b -= 128'sd1 << width;
    product = signed_a * signed_b;
    return 64'(product) & destination_mask;
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
    int address, left_address, right_address, width, code, compressed_count;
    logic [63:0] mask, a, b;
    logic [15:0] compress_mask;
    for (int i = 0; i < 16384; i++) program_words[i] = 'h0000006f;
    for (int i = 0; i < 8192; i++) memory_words[i] = 0;
    compress_mask=16'ha55a;
    memory_words[13'(('h1e000-'h10000)>>3)]=64'(compress_mask);
    for(int i=0;i<16;i++) begin
      memory_element('h1e080+i,64'(i+1),0);
      memory_element('h1e100+i,64'hee,0);
    end
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
      // Fractional multiply reuses the shared iterative multiplier, then
      // rounds its full product and reports delayed saturation at ordered drain.
      for (int round_mode = 0; round_mode < 4; round_mode++) begin
        write_vector_csr('h009,0); write_vector_csr('h00a,round_mode);
        vec('h27,24,8,8,0,0);
        vstore(24,address,sew);
        for (int lane=0;lane<16;lane++) expect_store(address+(lane<<sew),fractional_multiply(sew,round_mode,left_value(lane,sew),left_value(lane,sew)),sew);
        address+=128; signature('h009,address,1); address+=8;
        write_vector_csr('h009,0); write_vector_csr('h00a,round_mode); li(5,-3);
        vec('h27,24,8,5,0,4);
        vstore(24,address,sew);
        for (int lane=0;lane<16;lane++) expect_store(address+(lane<<sew),fractional_multiply(sew,round_mode,left_value(lane,sew),-64'd3),sew);
        address+=128; signature('h009,address,0); address+=8;
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
    // Narrow-source widening uses the ordinary Decode/unroller/WB path and
    // stores through doubled EEW/EMUL. A taken branch must squash a younger op.
    for (int sew = 0; sew < 3; sew++) begin
      width = 8 << sew;
      vset(sew,16,2); vload(8,'h10000+sew*256,sew); vload(16,'h10080+sew*256,sew);
      for (int op = 'h30; op <= 'h33; op++) begin
        for (int vx = 0; vx < 2; vx++) begin
          li(5,-3);
          vec(op,24,8,vx != 0 ? 5 : 16,0,vx != 0 ? 6 : 2);
          emit('h00000463); vec(op ^ 2,24,16,vx != 0 ? 5 : 8,0,vx != 0 ? 6 : 2);
          vstore(24,address,sew+1);
          for (int lane=0;lane<16;lane++)
            expect_store(address+(lane<<(sew+1)),widening_value(op,width,left_value(lane,sew),vx != 0 ? -64'd3 : right_value(lane)),sew+1);
          address+=128;
        end
      end
    end
    // Widening multiply keeps source SEW in the shared request while its
    // completion writes a doubled-width destination element and EMUL group.
    for (int sew = 0; sew < 3; sew++) begin
      width = 8 << sew;
      vset(sew,16,2); vload(8,'h10000+sew*256,sew); vload(16,'h10080+sew*256,sew);
      for (int operation = 0; operation < 3; operation++) begin
        code = operation == 0 ? 'h38 : 'h39 + operation;
        for (int vx = 0; vx < 2; vx++) begin
          li(5,-3);
          vec(code,24,8,vx != 0 ? 5 : 16,0,vx != 0 ? 6 : 2);
          li(5,9);
          emit('h00000463); vec(code,24,16,vx != 0 ? 5 : 8,0,vx != 0 ? 6 : 2);
          vstore(24,address,sew+1);
          for (int lane=0;lane<16;lane++)
            expect_store(address+(lane<<(sew+1)),widening_multiply_value(code,width,left_value(lane,sew),vx != 0 ? -64'd3 : right_value(lane)),sew+1);
          address+=128;
        end
      end
    end
    // Fractional source LMUL widens into one full destination register.
    for (int sew = 0; sew < 3; sew++) begin
      int element_count;
      width = 8 << sew; element_count = 8 >> sew;
      vset(sew,element_count,7); vload(8,'h10000+sew*256,sew); vload(16,'h10080+sew*256,sew);
      vec('h3a,24,8,16,0,2);
      vstore(24,address,sew+1);
      for (int lane=0;lane<element_count;lane++)
        expect_store(address+(lane<<(sew+1)),widening_multiply_value('h3a,width,left_value(lane,sew),right_value(lane)),sew+1);
      address+=128;
    end
    // Wide-source widening reads vs2 at the destination EEW while retaining
    // a narrow vector/scalar second operand and the ordinary WB beat schedule.
    for (int sew = 0; sew < 3; sew++) begin
      int wide_address;
      width = 8 << sew; wide_address = 'h11000 + sew*256;
      for (int lane = 0; lane < 16; lane++) memory_element(wide_address+(lane<<(sew+1)),left_value(lane,sew+1),sew+1);
      vset(sew,16,2); vload(8,wide_address,sew+1); vload(16,'h10080+sew*256,sew);
      for (int op = 'h34; op <= 'h37; op++) begin
        for (int wx = 0; wx < 2; wx++) begin
          li(5,-3);
          vec(op,24,8,wx != 0 ? 5 : 16,0,wx != 0 ? 6 : 2);
          vstore(24,address,sew+1);
          for (int lane=0;lane<16;lane++)
            expect_store(address+(lane<<(sew+1)),widening_value(op,width,left_value(lane,sew+1),wx != 0 ? -64'd3 : right_value(lane)),sew+1);
          address+=128;
        end
      end
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
    // Queries share normal GPR retirement; prefixes and indices use packed VRF writes.
    begin
      logic [63:0] source_mask, prefix;
      int count, first_set;
      source_mask=0; count=0; first_set=-1;
      vset(0,16); vload(8,'h10000,0); vec('h1f,0,8,0,0,3);
      for(int i=0;i<16;i++) begin
        a=left_value(i,0)&255;
        if(a!=0 && !a[7]) begin source_mask[i]=1; count++; if(first_set<0) first_set=i; end
      end
      for(int masked=0;masked<2;masked++) begin
        for(int query=0;query<2;query++) begin
          li(5,7); li(6,13); emit('h026282b3); // older deferred WAW
          vec(16,5,0,16+query,1'(masked),2);
          emit('h00128313); scalar_signature(6,address,(query==0 ? 64'(count) : 64'(first_set))+64'd1); address+=8;
        end
      end
      vec(16,0,0,16,0,2); scalar_signature(0,address,0); address+=8;
      li(5,9); emit('h00000463); vec(16,5,0,16,0,2);
      scalar_signature(5,address,9); address+=8;
      for(int op=0;op<3;op++) begin
        vset(0,16); vec(20,3,0,op==0 ? 1 : op==1 ? 3 : 2,0,2);
        prefix=0;
        for(int i=0;i<16;i++) prefix[i]=op==0 ? first_set<0 || i<first_set : op==1 ? first_set<0 || i<=first_set : i==first_set;
        vset(0,2); vstore(3,address,0);
        expect_store(address,prefix&255,0); expect_store(address+1,(prefix>>8)&255,0); address+=8;
      end
      for(int op=0;op<2;op++) begin
        count=0; vset(0,16); vec(20,16,0,16+op,0,2); vstore(16,address,0);
        for(int i=0;i<16;i++) begin
          expect_store(address+i,64'(op==0 ? count : i),0); if(source_mask[i]) count++;
        end
        address+=16;
      end
      vset(0,0);
      for(int query=0;query<2;query++) begin
        vec(16,5,0,16+query,0,2); scalar_signature(5,address,query==0 ? 0 : '1); address+=8;
      end
      for(int op=0;op<6;op++) begin
        int selector;
        selector=op==0 ? 16 : op==1 ? 17 : op==2 ? 1 : op==3 ? 3 : op==4 ? 2 : 16;
        vset(0,op%2==0 ? 16 : 0); emit('h0080d073);
        vec(op<2 ? 16 : 20,op<2 ? 5 : 16,0,selector,0,2); expect_store('h2fff0,2,3);
        signature('h008,address,0); address+=8;
      end
    end
    // Slides use captured scalar operands and survive scalar-LSU backpressure.
    // Initialize beyond VL so slidedown must read source elements past VL.
    for(int sew=0;sew<4;sew++) begin
      for(int form=0;form<6;form++) begin
        int dest, mode;
        bit up, one, masked;
        logic [63:0] value;
        up=form inside {0,1,4}; one=form>=4; masked=form[0];
        dest=up ? 16 : 8; mode=one ? 6 : form inside {1,3} ? 3 : 4;
        width=8<<sew; mask='1>>(64-width);
        vset(sew,16,3); vload(8,'h10000+sew*256,sew); vload(16,'h10080+sew*256,sew);
        vec(30,0,8,0,0,3); // vmsgtu.vi v0,v8,0
        vset(sew,13,3);
        li(5,one ? -19 : 3);
        if(mode==4) begin li(5,1); li(6,3); emit('h026282b3); end // deferred scalar producer
        emit('h0080d073); // vstart=1 preserves the first destination element
        vec(up ? 14 : 15,dest,8,mode==3 ? 3 : 5,masked,mode);
        emit('h00000463); vec(15,dest,8,5,0,4); // squashed slide must preserve the observed destination
        signature('h008,address,0); address+=8;
        vset(sew,16,3); vstore(dest,address,sew);
        for(int i=0;i<16;i++) begin
          value=up ? right_value(i) : left_value(i,sew);
          if(i>=1 && i<13 && (!masked || (left_value(i,sew)&mask)!=0) && (!up || one || i>=3)) begin
            if(one && i==(up ? 0 : 12)) value=-64'd19;
            else value=left_value(up ? i-(one ? 1 : 3) : i+(one ? 1 : 3),sew);
          end
          expect_store(address+(i<<sew),value&mask,sew);
        end
        address+=128;
      end
      vset(sew,0,3); vec(14,8,8,0,0,4); expect_store('h2fff0,2,3); // reserved overlap even with VL=0
    end
    // Gather index producers, deferred scalar index dependencies, nonzero
    // restart, masking, and squash all cross the real scalar/vector boundary.
    for(int sew=0;sew<4;sew++) for(int form=0;form<4;form++) begin
      logic [63:0] value;
      int iw;
      width=8<<sew; mask='1>>(64-width); iw=form==1 ? 1 : sew;
      vset(iw,8,2); vec(20,24,0,17,0,2); // vid.v supplies indices 0..7
      vset(sew,8,2); vload(8,'h10000+sew*256,sew); vload(16,'h10080+sew*256,sew);
      vec(30,0,8,0,0,3);
      vset(sew,7,2); emit('h0080d073); // preserve destination element zero
      li(5,1); li(6,7); emit('h026282b3); // gather must wait for deferred x5=7
      vec(form==1 ? 14 : 12,16,8,form<2 ? 24 : form==3 ? 7 : 5,form[0],form<2 ? 0 : form==2 ? 4 : 3);
      emit('h00000463); vec(12,16,8,0,0,4); // a squashed gather must not splat source zero
      signature('h008,address,0); address+=8;
      vset(sew,8,2); vstore(16,address,sew);
      for(int i=0;i<8;i++) begin
        value=right_value(i);
        if(i>=1 && i<7 && (!form[0] || (left_value(i,sew)&mask)!=0)) value=left_value(form<2 ? i : 7,sew);
        expect_store(address+(i<<sew),value&mask,sew);
      end
      address+=128;
      vset(sew,0,2); vec(12,8,8,0,0,4); expect_store('h2fff0,2,3);
    end
    // vcompress.vm packs selected source elements, preserves destination tails,
    // traps on nonzero vstart, and remains squashable before its first WB beat.
    vset(3,2); vload(5,'h1e000,3);
    vset(0,16); vload(8,'h1e080,0); vload(24,'h1e100,0);
    vec(23,24,8,5,0,2); vstore(24,address,0);
    compressed_count=0;
    for(int i=0;i<16;i++) if(compress_mask[i]) begin
      expect_store(address+compressed_count,64'(i+1),0); compressed_count++;
    end
    for(int i=compressed_count;i<16;i++) expect_store(address+i,64'hee,0);
    address+=16;
    vload(24,'h1e100,0); emit('h00000463); vec(23,24,8,5,0,2); vstore(24,address,0);
    for(int i=0;i<16;i++) expect_store(address+i,64'hee,0);
    address+=16;
    li(1,1); emit('h00809073); vec(23,24,8,5,0,2); expect_store('h2fff0,2,3);
    vstore(24,address,0);
    for(int i=0;i<16;i++) expect_store(address+i,64'hee,0);
    address+=16; signature('h008,address,0); address+=8;
    vset(0,0); vec(23,8,8,5,0,2); expect_store('h2fff0,2,3);
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
            $display("rv5stage vector integer/shared execution paths passed: %0d stores, %0d cycles", expected_count, cycles);
            $finish;
          end
        end
      end
      if (cycles > 300000) $fatal(1, "vector muldiv timeout stores=%0d fetch=%h", stores, instruction_access_out.request.bits.address);
    end
  end
endmodule
