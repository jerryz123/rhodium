// Models architectural elements independently of the packed datapath, including retry and cancellation.
// SPDX-License-Identifier: Apache-2.0
  localparam int CW = $clog2(VLEN + 1), AW = $clog2(32 * VLEN / 64), DEPTH = 32 * VLEN / 64;
  typedef struct packed { logic [AW-1:0] address; logic [63:0] data, mask; } write_t;
  typedef struct packed { logic valid; write_t bits; } write_port_t;
  typedef struct packed { logic [CW-1:0] first, ending; logic last, reduction, scan; logic [63:0] scan_carry; logic scalar_destination; logic [4:0] destination; logic memory, floating_point, multiply_divide, divide, store; logic [5:0] shift; write_t write; logic [63:0] compress_data; logic [3:0] compress_count; logic [CW-1:0] compress_destination; } result_t;
  logic clock = 0, reset = 1;
  logic [31:0] instruction;
  logic [XLEN-1:0] vtype, vl, vstart, scalar;
  logic request_valid, issue_ready, cancel, retry_enable;
  logic [CW-1:0] retry_first;
  write_port_t initialize_in;
  logic active, request_ready, legal, issued, committed, retried;
  result_t result;
  RV5StageVectorUnrollerFixture dut (.*);
  always #5 clock = ~clock;
  logic [63:0] memory [DEPTH], snapshot [DEPTH];
  logic [63:0] rng = 64'h713bfd9167c282c9;
  int tx_source_width, tx_width, tx_lanes, tx_vl, tx_start, tx_first, tx_opcode, tx_mode, tx_vd, tx_vs1, tx_vs2;
  logic [63:0] tx_scalar, tx_distance;
  int tx_vlmax;
  bit tx_masked, tx_compare, tx_mask_logic, tx_dense, tx_gather, tx_gather_vector, tx_compress, tx_widening, tx_narrowing, tx_wide_source, tx_widen_signed, checking;
  logic [127:0] tx_compress_buffer;
  int tx_compress_count, tx_compress_destination;
  int tx_beats;
  int checks = 0, macros = 0, retries = 0, cycles = 0, last_commit_cycle, consecutive = 0;

  function automatic logic [63:0] random_word();
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return rng;
  endfunction
  function automatic logic [63:0] element(input int regno, index, width_bits);
    return (snapshot[regno * VLEN / 64 + index * width_bits / 64] >> (index * width_bits % 64)) & (64'hffffffffffffffff >> (64 - width_bits));
  endfunction
  function automatic logic signed [63:0] signed_element(input logic [63:0] value, input int width_bits);
    return $signed(value << (64 - width_bits)) >>> (64 - width_bits);
  endfunction
  task automatic tick;
    logic [63:0] a, b, value, lane_mask, expected_data, expected_mask, broadcast_value;
    int first, ending, address, bit_offset, position, emitted, left_width;
    bit enabled, was_retry;
    @(negedge clock); #1;
    was_retry = retried;
    if (!reset) begin
      if (initialize_in.valid)
        memory[initialize_in.bits.address] = (memory[initialize_in.bits.address] & ~initialize_in.bits.mask) | (initialize_in.bits.data & initialize_in.bits.mask);
      if (retried) retries++;
      if (committed) begin
        assert (checking) else $fatal(1, "write escaped a cancelled macro");
        first = tx_first;
        ending = tx_start >= tx_vl ? tx_vl : ((first + tx_lanes < tx_vl) ? first + tx_lanes : tx_vl);
        address = tx_vd * VLEN / 64 + (tx_compare || tx_mask_logic ? first / 64 : first * tx_width / 64);
        expected_data = 0; expected_mask = 0;
        lane_mask = 64'hffffffffffffffff >> (64 - tx_width);
        broadcast_value = tx_scalar;
        if (tx_mode == 3) broadcast_value = (tx_opcode >= 37 && tx_opcode <= 45) ? 64'(tx_vs1) : {{59{tx_vs1[4]}}, 5'(tx_vs1)};
        if (tx_compress) begin
          if (first < tx_vl) begin
            for (int lane = 0; lane < tx_lanes && first + lane < ending; lane++) begin
              position = first + lane;
              if (snapshot[tx_vs1 * VLEN / 64 + position / 64][position % 64]) begin
                tx_compress_buffer |= 128'(element(tx_vs2, position, tx_width)) << (tx_compress_count * tx_width);
                tx_compress_count++;
              end
            end
          end
          emitted = tx_compress_count >= tx_lanes ? tx_lanes : ending == tx_vl ? tx_compress_count : 0;
          for (int lane = 0; lane < emitted; lane++) expected_mask |= lane_mask << (lane * tx_width);
          expected_data = tx_compress_buffer[63:0] & expected_mask;
          address = tx_vd * VLEN / 64 + tx_compress_destination * tx_width / 64;
          tx_compress_buffer >>= emitted * tx_width;
          tx_compress_count -= emitted;
          tx_compress_destination += emitted;
        end else for (int lane = 0; lane < tx_lanes; lane++) begin
          position = first + lane;
          enabled = position >= tx_start && position < tx_vl && (!tx_masked || tx_opcode == 23 || snapshot[position / 64][position % 64]);
          if (tx_opcode == 14 && tx_mode != 6 && !tx_gather && 64'(position) < tx_distance) enabled = 0;
          if (enabled) begin
            left_width = tx_narrowing ? 2 * tx_width : tx_wide_source ? tx_width : tx_source_width;
            a = element(tx_vs2, position, left_width);
            b = tx_mode inside {0, 2} || tx_mask_logic ? element(tx_vs1, position, tx_source_width) : broadcast_value & (64'hffffffffffffffff >> (64 - tx_source_width));
            if (tx_widen_signed) begin
              a = signed_element(a, tx_wide_source ? tx_width : tx_source_width);
              b = signed_element(b, tx_source_width);
            end
            if (tx_mask_logic) begin
              case (tx_opcode)
                24: value = a & ~b;
                25: value = a & b;
                26: value = a | b;
                27: value = a ^ b;
                28: value = a | ~b;
                29: value = ~(a & b);
                30: value = ~(a | b);
                31: value = ~(a ^ b);
                default: $fatal(1, "bad mask opcode");
              endcase
            end else if (tx_gather) begin
              b=tx_gather_vector ? element(tx_vs1,position,tx_opcode==14 ? 16 : tx_width) : tx_distance;
              value=b>=64'(tx_vlmax) ? 0 : element(tx_vs2,int'(b),tx_width);
            end else if (tx_narrowing) begin
              if (tx_opcode[0]) value = signed_element(a, left_width) >>> (b & 64'(left_width - 1));
              else value = a >> (b & 64'(left_width - 1));
            end else if (tx_widening) begin
              value = tx_opcode[1] ? a - b : a + b;
            end else case (tx_opcode)
              0: value = a + b;
              2: value = a - b;
              4: value = a < b ? a : b;
              5: value = signed_element(a, tx_width) < signed_element(b, tx_width) ? a : b;
              6: value = a > b ? a : b;
              7: value = signed_element(a, tx_width) > signed_element(b, tx_width) ? a : b;
              9: value = a & b;
              10: value = a | b;
              11: value = a ^ b;
              14: value = tx_mode == 6 && position == 0 ? broadcast_value : element(tx_vs2,position-int'(tx_distance),tx_width);
              15: begin
                if (tx_mode == 6 && position == tx_vl-1) value = broadcast_value;
                else if (tx_distance >= 64'(tx_vlmax) || 64'(position) >= 64'(tx_vlmax)-tx_distance) value = 0;
                else value = element(tx_vs2,position+int'(tx_distance),tx_width);
              end
              23: value = !tx_masked || snapshot[position / 64][position % 64] ? b : a;
              24: value = 64'(a == b);
              25: value = 64'(a != b);
              26: value = 64'(a < b);
              27: value = 64'(signed_element(a, tx_width) < signed_element(b, tx_width));
              28: value = 64'(a <= b);
              29: value = 64'(signed_element(a, tx_width) <= signed_element(b, tx_width));
              30: value = 64'(a > b);
              31: value = 64'(signed_element(a, tx_width) > signed_element(b, tx_width));
              37: value = a << (b & 64'(tx_width - 1));
              40: value = a >> (b & 64'(tx_width - 1));
              41: value = signed_element(a, tx_width) >>> (b & 64'(tx_width - 1));
              default: $fatal(1, "bad reference opcode");
            endcase
            bit_offset = tx_compare ? position % 64 : tx_gather_vector || tx_narrowing ? position*tx_width%64 : lane * tx_width;
            expected_data |= (value & (tx_compare ? 64'd1 : lane_mask)) << bit_offset;
            expected_mask |= (tx_compare ? 64'd1 : lane_mask) << bit_offset;
          end
        end
        assert (int'(result.first) == first && int'(result.ending) == ending && result.last == (ending == tx_vl && (!tx_compress || tx_compress_count == 0)))
          else $fatal(1, "element progress first=%0d/%0d end=%0d/%0d", result.first, first, result.ending, ending);
        assert (result.write.mask == expected_mask && (result.write.data & expected_mask) == expected_data)
          else $fatal(1, "op=%0d SEW=%0d first=%0d data=%h/%h mask=%h/%h", tx_opcode, tx_width, first, result.write.data & expected_mask, expected_data, result.write.mask, expected_mask);
        if (expected_mask != 0) begin
          assert (int'(result.write.address) == address) else $fatal(1, "wrong destination row");
          memory[address] = (memory[address] & ~expected_mask) | expected_data;
        end
        if (tx_compress) begin
          assert(result.compress_data == tx_compress_buffer[63:0] && result.compress_count == 4'(tx_compress_count) && int'(result.compress_destination) == tx_compress_destination)
            else $fatal(1,"compress checkpoint data=%h/%h count=%0d/%0d destination=%0d/%0d",result.compress_data,tx_compress_buffer[63:0],result.compress_count,tx_compress_count,result.compress_destination,tx_compress_destination);
        end
        if (tx_dense && tx_beats != 0)
          assert (last_commit_cycle + (tx_gather_vector ? 2 : 1) == cycles) else $fatal(1, "bubble in an unstalled vector stream");
        tx_beats++;
        if (last_commit_cycle + 1 == cycles) consecutive++;
        last_commit_cycle = cycles;
        tx_first = ending;
        checks++;
        if (result.last) checking = 0;
      end
    end
    @(posedge clock); #1;
    if (was_retry) retry_enable = 0;
    cycles++;
  endtask

  task automatic run_macro(input int sew, lmul, count, start, op, mode, destination, source1, source2,
                           input bit masked_op, inject_retry = 0, random_stalls = 1, input int retry_at = -1);
    int timeout;
    assert (!active && !checking) else $fatal(1, "previous macro did not drain");
    tx_widening = op inside {[48:55]}; tx_narrowing = op inside {44, 45}; tx_wide_source = op inside {[52:55]} || tx_narrowing; tx_widen_signed = tx_widening && op[0];
    tx_compress = op == 23 && mode == 2;
    tx_mask_logic = mode == 2 && !tx_compress && !tx_widening && !tx_narrowing;
    tx_gather = op==12 || (op==14 && mode==0); tx_gather_vector=tx_gather && mode==0;
    tx_source_width = tx_mask_logic ? 1 : 8 << sew; tx_width = tx_widening ? 2 * tx_source_width : tx_source_width; tx_lanes = tx_gather_vector ? 1 : 64 / (tx_narrowing ? 2 * tx_width : tx_width);
    tx_vl = count; tx_start = start; tx_first = start / tx_lanes * tx_lanes;
    tx_opcode = op; tx_mode = mode; tx_vd = destination; tx_vs1 = source1; tx_vs2 = source2;
    tx_masked = masked_op; tx_compare = !tx_mask_logic && op >= 24 && op <= 31;
    tx_dense = !random_stalls && !inject_retry; tx_beats = 0;
    tx_compress_buffer = 0; tx_compress_count = 0; tx_compress_destination = 0;
    tx_scalar = XLEN == 32 ? {{32{scalar[31]}}, scalar[31:0]} : 64'(scalar);
    tx_distance = mode == 6 ? 1 : mode == 3 ? 64'(source1) : 64'(scalar);
    tx_vlmax = lmul < 4 ? (VLEN / (8 << sew)) << lmul : (VLEN / (8 << sew)) >> (8-lmul);
    for (int row = 0; row < DEPTH; row++) snapshot[row] = memory[row];
    instruction = (32'(op) << 26) | (32'(!masked_op) << 25) | (32'(source2) << 20) | (32'(source1) << 15) | (32'(mode) << 12) | (32'(destination) << 7) | 32'h57;
    vtype = (XLEN'(sew) << 3) | XLEN'(lmul); vl = XLEN'(count); vstart = XLEN'(start);
    request_valid = 1; issue_ready = 1; retry_enable = inject_retry; retry_first = CW'(retry_at < 0 ? tx_lanes : retry_at);
    checking = 1;
    #1; assert (request_ready && legal) else $fatal(1, "illegal test instruction %h vtype %h", instruction, vtype);
    tick(); request_valid = 0;
    // Mutate live inputs immediately: all execution must use the captured descriptor.
    instruction = 0; vtype = 0; vl = 0; vstart = 0; scalar = ~scalar;
    timeout = 0;
    while (active || checking) begin
      issue_ready = !random_stalls || (random_word() % 4 != 0);
      tick();
      if (timeout++ > 4000) $fatal(1, "unroller failed to drain");
    end
    repeat (5) tick();
    macros++;
  endtask

  initial begin
    instruction = 0; vtype = 0; vl = 0; vstart = 0; scalar = XLEN'(-17);
    request_valid = 0; issue_ready = 0; cancel = 0; retry_enable = 0; retry_first = 0;
    initialize_in = '0; checking = 0; last_commit_cycle = -100;
    repeat (3) tick(); reset = 0;
    for (int row = 0; row < DEPTH; row++) begin
      initialize_in.valid = 1;
      initialize_in.bits = '{AW'(row), random_word(), 64'hffffffffffffffff};
      tick();
    end
    initialize_in.valid = 0;
    // Every SEW/LMUL geometry, including fractional groups, in-place operands,
    // vstart in the middle of a beat, tails, and signed scalar extension.
    for (int sew = 0; sew < 4; sew++) begin
      for (int lm = 0; lm < 8; lm++) begin
        int maximum;
        if (lm == 4 || (lm >= 5 && sew > lm - 5)) continue;
        maximum = VLEN / (8 << sew);
        maximum = lm < 4 ? maximum << lm : maximum >> (8 - lm);
        if (maximum == 0) continue;
        run_macro(sew, lm, maximum, 0, 0, 0, 24, 16, 8, 0, maximum > 64 / (8 << sew));
        run_macro(sew, lm, maximum - 1, maximum > 2 ? 1 : 0, 11, 4, 8, 3, 8, 1);
      end
      for (int op = 0; op < 42; op++) begin
        if (!(op inside {0, 2, [4:7], [9:11], [24:31], 37, 40, 41})) continue;
        for (int mode_index = 0; mode_index < 3; mode_index++) begin
          int mode;
          mode = mode_index == 0 ? 0 : mode_index == 1 ? 4 : 3;
          if ((op inside {30, 31}) && mode == 0) continue;
          if (mode == 3 && (op inside {2, [4:7], 26, 27})) continue;
          run_macro(sew, 0, VLEN / (8 << sew), 1, op, mode, op inside {[24:31]} ? 0 : 24, mode == 0 ? 16 : 31, 8, 0);
        end
      end
      run_macro(sew, 0, 0, 0, 0, 0, 24, 16, 8, 0);
      run_macro(sew, 0, 1, 7, 0, 0, 24, 16, 8, 0);
    end
    // Narrow+narrow widening add/sub uses one destination-width beat per
    // source half. Exercise every legal SEW/LMUL, signedness, form, masks,
    // tails, vstart, upper-half retry, and the permitted high-source overlap.
    for (int sew = 0; sew < 3; sew++) begin
      for (int lm = 0; lm < 3; lm++) begin
        int maximum, lanes, count;
        maximum = (VLEN / (8 << sew)) << lm;
        lanes = 4 >> sew;
        count = maximum < 2 * lanes + 1 ? maximum : 2 * lanes + 1;
        for (int op = 48; op < 52; op++) begin
          run_macro(sew, lm, count, op[0] ? 1 : 0, op, 2, 24, 16, 8, op[0], op == 49, op != 50, lanes);
          scalar = XLEN'(-17);
          run_macro(sew, lm, count - 1, count > 2 ? lanes - 1 : 0, op, 6, 24, 3, 8, op[0]);
        end
      end
      run_macro(sew, 0, VLEN / (8 << sew), 0, 51, 2, 8, 16, 9, 0, 1, 0, 4 >> sew);
    end
    // Wide+narrow widening reuses the destination-width schedule. vs2 reads
    // one wide row per beat while vector/scalar vs1 still selects a narrow half.
    for (int sew = 0; sew < 3; sew++) begin
      for (int lm = 0; lm < 3; lm++) begin
        int maximum, lanes, count;
        maximum = (VLEN / (8 << sew)) << lm;
        lanes = 4 >> sew;
        count = maximum < 2 * lanes + 1 ? maximum : 2 * lanes + 1;
        for (int op = 52; op < 56; op++) begin
          run_macro(sew, lm, count, op[0] ? 1 : 0, op, 2, 24, 16, 8, op[0], op == 53, op != 54, lanes);
          scalar = XLEN'(-17);
          run_macro(sew, lm, count - 1, count > 2 ? lanes - 1 : 0, op, 6, 24, 3, 8, op[0]);
        end
      end
      // Equal-width destination/vs2 overlap and high-part narrow-vs1 overlap.
      run_macro(sew, 0, VLEN / (8 << sew), 0, 55, 2, 8, 9, 8, 0, 1, 0, 4 >> sew);
    end
    // Narrowing shifts consume one doubled-width source row and write one
    // destination half-row per beat through the existing SIMD shifter.
    for (int sew = 0; sew < 3; sew++) begin
      for (int lm_index = 0; lm_index < 6; lm_index++) begin
        int lm, exponent, maximum, lanes, count;
        lm = lm_index < 3 ? lm_index : lm_index + 2;
        exponent = lm < 4 ? lm : lm - 8;
        if (sew > exponent + 3) continue;
        maximum = exponent >= 0 ? (VLEN / (8 << sew)) << exponent : (VLEN / (8 << sew)) >> -exponent;
        if (maximum == 0) continue;
        lanes = 4 >> sew;
        count = maximum < 2 * lanes + 1 ? maximum : 2 * lanes + 1;
        for (int op = 44; op < 46; op++) begin
          run_macro(sew, lm, count, op[0] ? 1 : 0, op, 0, 24, 16, 8, op[0], op == 44, 0, lanes);
          scalar = XLEN'(-17);
          run_macro(sew, lm, count - 1, count > 2 ? lanes - 1 : 0, op, 4, 24, 3, 8, !op[0]);
          run_macro(sew, lm, count, 0, op, 3, 24, 31, 8, op[0]);
        end
      end
      // Low-part in-place overlap remains safe across authorized-prefix retry.
      run_macro(sew, 0, VLEN / (8 << sew), 0, 45, 0, 8, 16, 8, 0, 1, 0, 4 >> sew);
    end
    // Moves and merge share an encoding but not predication: a zero v0 bit
    // selects vs2; it must not disable the destination write.
    for (int sew = 0; sew < 4; sew++) begin
      for (int lm = 0; lm < 8; lm++) begin
        int maximum;
        if (lm == 4 || (lm >= 5 && sew > lm - 5)) continue;
        maximum = lm < 4 ? ((VLEN / (8 << sew)) << lm) : ((VLEN / (8 << sew)) >> (8 - lm));
        for (int form = 0; form < 3; form++) begin
          int mode, src;
          mode = form == 0 ? 0 : form == 1 ? 4 : 3;
          src = form == 0 ? 16 : 31;
          run_macro(sew, lm, maximum, 0, 23, mode, 24, src, 0, 0);
          run_macro(sew, lm, maximum - 1, maximum > 2 ? 1 : 0, 23, mode, 8, src, 8, 1);
          // Legal moves to v0 and in-place vector-source moves.
          run_macro(sew, lm, maximum, 0, 23, mode, form == 0 ? 16 : 0, src, 0, 0);
        end
      end
      // All mask instructions address single registers, even with LMUL=8.
      // Vary SEW while keeping valid VL and sweep in-place operands/destination v0.
      for (int op = 24; op < 32; op++) begin
        int maximum;
        maximum = VLEN >> sew;
        run_macro(sew, 3, maximum, 0, op, 2, 3, 5, 7, 0, 0, 0);
        run_macro(sew, 3, maximum - 1, 3, op, 2, 5, 5, 7, 0);
        run_macro(sew, 3, maximum, maximum > 64 ? 63 : 1, op, 2, 0, 5, 0, 0, 1, 1, 0);
      end
      run_macro(sew, 0, 0, 7, 23, 3, 0, 31, 0, 0);
      run_macro(sew, 0, 1, 7, 23, 4, 8, 3, 8, 1);
      run_macro(sew, 0, 0, 7, 31, 2, 0, 5, 7, 0);
    end
    // Slides read across chunks/groups while rotating through the same E64
    // SIMD slot. Golden values come from the original architectural snapshot.
    for (int sew=0;sew<4;sew++) begin
      for (int lm=0;lm<8;lm++) begin
        int maximum, lanes;
        if (lm==4 || (lm>=5 && sew>lm-5)) continue;
        maximum=lm<4 ? (VLEN/(8<<sew))<<lm : (VLEN/(8<<sew))>>(8-lm);
        lanes=8>>sew;
        for (int form=0;form<6;form++) begin
          int op, mode, dest;
          op=form inside {0,1,4} ? 14 : 15;
          mode=form>=4 ? 6 : form inside {1,3} ? 3 : 4;
          for (int scenario=0;scenario<12;scenario++) begin
            int amount, length, start;
            bit masked;
            amount=scenario<8 ? scenario : scenario==8 ? 31 : scenario==9 ? maximum-1 : scenario==10 ? maximum : maximum+1;
            scalar=mode==6 ? XLEN'(-17) : XLEN'(amount);
            dest=op==15 && scenario[0] ? 8 : 24;
            length=scenario==2 ? 0 : scenario==3 ? 1 : scenario==4 ? maximum-1 : maximum;
            start=scenario==5 ? 1 : scenario==6 ? lanes-1 : scenario==7 ? length : 0;
            masked=scenario[0];
            run_macro(sew,lm,length,start,op,mode,dest,mode==3 ? amount&31 : 3,8,masked,0,scenario!=0);
          end
          // Bit 8 and the XLEN sign bit must not truncate to SEW or an address.
          if (mode==4) begin
            scalar=XLEN'(256); run_macro(sew,lm,maximum,0,op,mode,24,3,8,0);
            scalar=XLEN'(1)<<(XLEN-1); run_macro(sew,lm,maximum,0,op,mode,24,3,8,0);
            scalar='1; run_macro(sew,lm,maximum,0,op,mode,24,3,8,0);
          end
          // Read-before-write ordering preserves an in-place downward suffix
          // when the first partial chunk or a later chunk must be reread.
          if (maximum>lanes) begin
            scalar=mode==6 ? XLEN'(-37) : 1;
            run_macro(sew,lm,maximum,lanes>1 ? 1 : 0,op,mode,op==15 ? 8 : 24,1,8,1,1,1,0);
            scalar=mode==6 ? XLEN'(-37) : 1;
            run_macro(sew,lm,maximum,lanes>1 ? 1 : 0,op,mode,op==15 ? 8 : 24,1,8,1,1,1,lanes);
          end
          scalar=mode==6 ? XLEN'(-17) : 3;
          run_macro(sew,lm,maximum,0,op,mode,op==15 ? 8 : 24,3,8,0,0,0);
        end
      end
    end
    // Gather reads arbitrary source positions, with a separate EEW16 index
    // stream. Source values are modeled from the admission-time snapshot.
    for(int sew=0;sew<4;sew++) begin
      for(int lm=0;lm<8;lm++) begin
        int maximum, exponent;
        exponent=lm<4 ? lm : lm-8;
        if(lm==4 || sew>exponent+3) continue;
        maximum=exponent>=0 ? (VLEN/(8<<sew))<<exponent : (VLEN/(8<<sew))>>(-exponent);
        for(int form=0;form<4;form++) begin
          int iw, groups, ig, op, mode;
          iw=form==1 ? 16 : 8<<sew; ig=exponent+(form==1 ? 1-sew : 0);
          if(form==1 && (ig < -3 || ig > 3)) continue;
          groups=ig>0 ? 1<<ig : 1; op=form==1 ? 14 : 12; mode=form<2 ? 0 : form==2 ? 4 : 3;
          // Refresh data/destination after the preceding destructive slide
          // sweeps, so indexed selection cannot pass on an all-zero source.
          for(int r=8;r<32;r++) begin
            if(r>=16 && r<24) continue;
            for(int row=0;row<VLEN/64;row++) begin
              initialize_in='{1'b1,'{AW'(r*VLEN/64+row),random_word(),64'hffffffffffffffff}}; tick();
            end
          end
          initialize_in.valid=0;
          if(form<2) begin
            for(int row=0;row<groups*VLEN/64;row++) begin
              logic [63:0] data, idx;
              data=0;
              for(int lane=0;lane<64/iw;lane++) begin
                int i;
                i=row*(64/iw)+lane;
                case(i%8)
                  0: idx=0;
                  1: idx=64'(maximum-1);
                  2: idx=64'(maximum);
                  3: idx='1;
                  4: idx=64'(256);
                  default: idx=random_word()%64'(maximum);
                endcase
                data|=(idx & ('1>>(64-iw)))<<(lane*iw);
              end
              initialize_in='{1'b1,'{AW'(16*VLEN/64+row),data,64'hffffffffffffffff}}; tick();
            end
            initialize_in.valid=0;
          end
          for(int scenario=0;scenario<9;scenario++) begin
            int length, start, lanes;
            lanes=form<2 ? 1 : 8>>sew;
            length=scenario==0 ? 0 : scenario==1 ? maximum-1 : maximum;
            start=scenario==2 ? 1 : scenario==3 ? maximum : 0;
            scalar=scenario==4 ? XLEN'(maximum-1) : scenario==5 ? XLEN'(maximum) : scenario==6 ? XLEN'(256) : scenario==7 ? XLEN'(1)<<(XLEN-1) : scenario==8 ? '1 : XLEN'(3);
            run_macro(sew,lm,length,start,op,mode,24,form<2 ? 16 : form==3 ? 31 : 3,8,scenario[0],0,scenario!=4);
            if(maximum>lanes && scenario==4) begin
              scalar=XLEN'(maximum-1);
              run_macro(sew,lm,maximum,0,op,mode,24,form<2 ? 16 : 3,8,1,1,1,lanes);
              scalar=XLEN'(maximum-1);
              run_macro(sew,lm,maximum,0,op,mode,24,form<2 ? 16 : 3,8,0,1,1,0);
            end
          end
          // Equal-EEW source aliases are legal; the destination stays disjoint.
          if(form==0 || (form==1 && sew==1))
            run_macro(sew,lm,maximum,0,op,0,24,8,8,0);
        end
      end
    end
    // Compress streams source chunks in order, checkpoints its packed suffix
    // at WB, and writes consecutive destination chunks without extra VRF ports.
    for(int sew=0;sew<4;sew++) begin
      for(int lm=0;lm<8;lm++) begin
        int exponent, maximum, lanes;
        exponent=lm<4 ? lm : lm-8;
        if(lm==4 || sew>exponent+3) continue;
        maximum=exponent>=0 ? (VLEN/(8<<sew))<<exponent : (VLEN/(8<<sew))>>(-exponent);
        lanes=8>>sew;
        for(int pattern=0;pattern<4;pattern++) begin
          for(int row=0;row<VLEN/64;row++) begin
            logic [63:0] mask_data;
            mask_data=pattern==0 ? 0 : pattern==1 ? '1 : pattern==2 ? 64'hd4924924a529294a : random_word();
            initialize_in='{1'b1,'{AW'(5*VLEN/64+row),mask_data,64'hffffffffffffffff}}; tick();
          end
          initialize_in.valid=0;
          run_macro(sew,lm,pattern==0 ? maximum : pattern==1 ? maximum-1 : maximum,0,23,2,24,5,8,0,pattern==3 && maximum>lanes,pattern!=2,pattern==3 ? lanes : -1);
        end
        run_macro(sew,lm,0,0,23,2,24,5,8,0);
      end
    end
    // Retry after an authorized in-place prefix and in its first partial row.
    for (int ones = 0; ones < 2; ones++) begin
      for (int row = 0; row < VLEN / 64; row++) begin
        initialize_in = '{1'b1, '{AW'(row), ones != 0 ? 64'hffffffffffffffff : 64'b0, 64'hffffffffffffffff}};
        tick();
      end
      initialize_in.valid = 0;
      run_macro(0,3,VLEN,0,23,0,8,16,8,1,0,0);
      scalar=1; run_macro(0,3,VLEN,0,14,4,24,3,8,1,0,0);
      scalar=1; run_macro(0,3,VLEN,0,15,4,8,3,8,1,0,0);
      scalar=XLEN'(-17); run_macro(0,3,VLEN,0,14,6,24,3,8,1,0,0);
      scalar=XLEN'(-17); run_macro(0,3,VLEN,0,15,6,8,3,8,1,0,0);
      scalar=3; run_macro(0,3,VLEN,0,12,4,24,3,8,1,0,0);
      run_macro(0,3,VLEN,0,12,0,24,16,8,1);
    end
    run_macro(0, 3, VLEN - 1, 3, 27, 2, 3, 5, 3, 0, 1, 1, 64);
    run_macro(0, 3, VLEN - 1, 3, 23, 0, 8, 16, 8, 1, 1, 1, 8);
    run_macro(0, 3, VLEN, 0, 0, 0, 24, 16, 8, 0, 0, 0);
    // Retry both the initial partial chunk and a later in-place chunk after
    // its prefix committed; neither case may rewrite pre-vstart elements.
    run_macro(0, 0, VLEN / 8, 3, 0, 4, 8, 3, 8, 0, 1, 1, 0);
    run_macro(0, 0, VLEN / 8, 3, 0, 4, 8, 3, 8, 0, 1, 1, 8);
    initialize_in = '{1'b1, '{AW'(0), 64'haaaaaaaaaaaaaaa5, 64'hffffffffffffffff}};
    tick(); initialize_in.valid = 0;
    run_macro(0, 0, VLEN / 8, 0, 25, 0, 0, 16, 8, 1);
    assert (consecutive >= VLEN / 8 - 1 && retries > 0) else $fatal(1, "missing throughput/retry coverage");
    // Cancel at read, buffered-offer, and pre-WB boundaries. No killed token
    // may update the bank or be mistaken for the next macro's response.
    for (int family = 0; family < 10; family++) begin
      for (int delay = 0; delay < 4; delay++) begin
      instruction = family == 0 ? 32'h02880c57 : family == 1 ? 32'h5c880c57 : family == 2 ? 32'h5e080c57 : family == 3 ? 32'h6e72a1d7 : family == 4 ? 32'h3a81cc57 : family == 5 ? 32'h3e81e457 : family == 6 ? 32'h32880c57 : family == 7 ? 32'h3a880c57 : family == 8 ? 32'h5e82ac57 : 32'hce816457;
      vtype = 0; vl = XLEN'(VLEN / 8); vstart = 0;
      request_valid = 1; issue_ready = delay == 3; tick(); request_valid = 0;
      repeat (delay) tick(); cancel = 1; tick(); cancel = 0;
      repeat (7) tick();
      end
    end
    for(int delay=1;delay<=3;delay++) begin
      instruction = 32'h32880c57; vtype = 0; vl = XLEN'(VLEN / 8); vstart = 0;
      request_valid = 1; issue_ready = 0; tick(); request_valid = 0;
      repeat (delay) tick(); reset = 1; tick(); reset = 0;
      repeat (7) tick();
    end
    run_macro(0, 0, VLEN / 8, 0, 11, 0, 8, 8, 8, 0);
    $display("vector unroller XLEN=%0d VLEN=%0d: %0d macros, %0d WB beats, %0d retries", XLEN, VLEN, macros, checks, retries);
    $finish;
  end
