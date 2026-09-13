// Models architectural elements independently of the packed datapath, including retry and cancellation.
// SPDX-License-Identifier: Apache-2.0
  localparam int CW = $clog2(VLEN + 1), AW = $clog2(32 * VLEN / 64), DEPTH = 32 * VLEN / 64;
  typedef struct packed { logic [AW-1:0] address; logic [63:0] data, mask; } write_t;
  typedef struct packed { logic valid; write_t bits; } write_port_t;
  typedef struct packed { logic [CW-1:0] first, ending; logic last; write_t write; } result_t;
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
  int tx_width, tx_lanes, tx_vl, tx_start, tx_first, tx_opcode, tx_mode, tx_vd, tx_vs1, tx_vs2;
  logic [63:0] tx_scalar;
  bit tx_masked, tx_compare, tx_dense, checking;
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
    int first, ending, address, bit_offset, position;
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
        address = tx_vd * VLEN / 64 + (tx_compare ? first / 64 : first * tx_width / 64);
        expected_data = 0; expected_mask = 0;
        lane_mask = 64'hffffffffffffffff >> (64 - tx_width);
        broadcast_value = tx_scalar;
        if (tx_mode == 3) broadcast_value = (tx_opcode >= 37 && tx_opcode <= 41) ? 64'(tx_vs1) : {{59{tx_vs1[4]}}, 5'(tx_vs1)};
        for (int lane = 0; lane < tx_lanes; lane++) begin
          position = first + lane;
          enabled = position >= tx_start && position < tx_vl && (!tx_masked || snapshot[position / 64][position % 64]);
          if (enabled) begin
            a = element(tx_vs2, position, tx_width);
            b = tx_mode == 0 ? element(tx_vs1, position, tx_width) : broadcast_value & lane_mask;
            case (tx_opcode)
              0: value = a + b;
              2: value = a - b;
              4: value = a < b ? a : b;
              5: value = signed_element(a, tx_width) < signed_element(b, tx_width) ? a : b;
              6: value = a > b ? a : b;
              7: value = signed_element(a, tx_width) > signed_element(b, tx_width) ? a : b;
              9: value = a & b;
              10: value = a | b;
              11: value = a ^ b;
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
            bit_offset = tx_compare ? position % 64 : lane * tx_width;
            expected_data |= (value & (tx_compare ? 64'd1 : lane_mask)) << bit_offset;
            expected_mask |= (tx_compare ? 64'd1 : lane_mask) << bit_offset;
          end
        end
        assert (int'(result.first) == first && int'(result.ending) == ending && result.last == (ending == tx_vl))
          else $fatal(1, "element progress first=%0d/%0d end=%0d/%0d", result.first, first, result.ending, ending);
        assert (result.write.mask == expected_mask && (result.write.data & expected_mask) == expected_data)
          else $fatal(1, "op=%0d SEW=%0d first=%0d data=%h/%h mask=%h/%h", tx_opcode, tx_width, first, result.write.data & expected_mask, expected_data, result.write.mask, expected_mask);
        if (expected_mask != 0) begin
          assert (int'(result.write.address) == address) else $fatal(1, "wrong destination row");
          memory[address] = (memory[address] & ~expected_mask) | expected_data;
        end
        if (tx_dense && tx_beats != 0)
          assert (last_commit_cycle + 1 == cycles) else $fatal(1, "bubble in an unstalled packed stream");
        tx_beats++;
        if (last_commit_cycle + 1 == cycles) consecutive++;
        last_commit_cycle = cycles;
        tx_first = ending;
        checks++;
        if (ending == tx_vl) checking = 0;
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
    tx_width = 8 << sew; tx_lanes = 64 / tx_width;
    tx_vl = count; tx_start = start; tx_first = start / tx_lanes * tx_lanes;
    tx_opcode = op; tx_mode = mode; tx_vd = destination; tx_vs1 = source1; tx_vs2 = source2;
    tx_masked = masked_op; tx_compare = op >= 24 && op <= 31;
    tx_dense = !random_stalls && !inject_retry; tx_beats = 0;
    tx_scalar = XLEN == 32 ? {{32{scalar[31]}}, scalar[31:0]} : 64'(scalar);
    for (int row = 0; row < DEPTH; row++) snapshot[row] = memory[row];
    instruction = (32'(op) << 26) | (32'(!masked_op) << 25) | (32'(source2) << 20) | (32'(source1) << 15) | (32'(mode) << 12) | (32'(destination) << 7) | 32'h57;
    vtype = (XLEN'(sew) << 3) | XLEN'(lmul); vl = XLEN'(count); vstart = XLEN'(start);
    request_valid = 1; issue_ready = 1; retry_enable = inject_retry; retry_first = CW'(retry_at < 0 ? tx_lanes : retry_at);
    checking = 1;
    #1; assert (request_ready && legal) else $fatal(1, "illegal test instruction %h vtype %h", instruction, vtype);
    tick(); request_valid = 0;
    // Mutate live inputs immediately: all execution must use the captured descriptor.
    instruction = 0; vtype = 0; vl = 0; vstart = 0;
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
    for (int delay = 0; delay < 4; delay++) begin
      instruction = 32'h02880c57; vtype = 0; vl = XLEN'(VLEN / 8); vstart = 0;
      request_valid = 1; issue_ready = delay == 3; tick(); request_valid = 0;
      repeat (delay) tick(); cancel = 1; tick(); cancel = 0;
      repeat (7) tick();
    end
    instruction = 32'h02880c57; vtype = 0; vl = XLEN'(VLEN / 8); vstart = 0;
    request_valid = 1; issue_ready = 0; tick(); request_valid = 0;
    repeat (3) tick(); reset = 1; tick(); reset = 0;
    repeat (7) tick();
    run_macro(0, 0, VLEN / 8, 0, 11, 0, 8, 8, 8, 0);
    $display("vector unroller XLEN=%0d VLEN=%0d: %0d macros, %0d WB beats, %0d retries", XLEN, VLEN, macros, checks, retries);
    $finish;
  end
