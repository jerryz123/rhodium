// Differentially checks flat vector storage, masked forwarding, packing, and SIMD writes at three VLENs.
module rv5stage_vector_tb;
  logic clock = 0, reset = 1;
  logic read_valid;
  logic [15:0] read_a;
  logic [15:0] read_b;
  logic [15:0] read_c;
  logic write_valid;
  logic [15:0] write_address;
  logic [63:0] write_data;
  logic [63:0] write_mask;
  logic commit;
  logic [4:0] destination;
  logic mask_destination;
  logic [1:0] element_width;
  logic [16:0] first_element;
  logic [16:0] vl;
  logic [16:0] vstart;
  logic [16:0] vlmax;
  logic masked;
  logic [1:0] operand_select;
  logic immediate_unsigned;
  logic widening;
  logic upper_half;
  logic [63:0] scalar;
  logic [4:0] immediate;
  logic [2:0] operation;
  logic [191:0] read_a_result;
  logic [191:0] read_b_result;
  logic [191:0] read_c_result;
  logic [2:0] result_valid;
  logic [2:0] result_legal;
  logic [47:0] result_address;
  logic [191:0] result_data;
  logic [191:0] result_mask;
  RV5StageVectorFixture dut (.*);
  always #5 clock = ~clock;
  logic [63:0] memory [0:255];
  logic [63:0] pending_data, pending_mask;
  logic [63:0] pending_reads [0:2];
  int pending_address;
  bit pending_valid, pending_legal;
  int bank_index, vlen_bits, depth, chunks;
  longint checks = 0;
  logic [63:0] rng = 64'h146b35e7da08c912;

  function automatic logic [63:0] random_word();
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
  endfunction

  task automatic step;
    logic [63:0] a, b, c, m, x, y, value, lane_mask, data, mask_bits, broadcast_value;
    logic [63:0] actual_data, actual_mask;
    int width_bits, out_width, lanes, first, source_lane, position, address;
    bit legal, enabled, expected_valid;
    #1;
    if (pending_valid) begin
      assert (read_a_result[bank_index*64 +: 64] == pending_reads[0] &&
              read_b_result[bank_index*64 +: 64] == pending_reads[1] &&
              read_c_result[bank_index*64 +: 64] == pending_reads[2])
        else $fatal(1, "later inputs changed an already captured read");
    end
    if (!reset) begin
      if (commit && pending_valid && pending_legal)
        memory[pending_address] = (memory[pending_address] & ~pending_mask) | (pending_data & pending_mask);
      else if (write_valid)
        memory[int'(write_address) % depth] = (memory[int'(write_address) % depth] & ~write_mask) | (write_data & write_mask);
    end
    a = memory[int'(read_a) % depth]; b = memory[int'(read_b) % depth];
    c = memory[int'(read_c) % depth]; m = c;
    width_bits = 8 << element_width;
    out_width = widening ? width_bits * 2 : width_bits;
    lanes = 64 / width_bits;
    first = int'(first_element) + ((widening && upper_half) ? lanes / 2 : 0);
    address = int'(destination) * chunks + (mask_destination ? first / 64 : first * out_width / 64);
    legal = int'(first_element) % lanes == 0 && vl <= vlmax && vlmax != 0 && int'(vlmax) <= vlen_bits &&
            first_element < vlmax && !(widening && width_bits == 64) &&
            address < depth && (!mask_destination || first < vlen_bits);
    data = 0; mask_bits = 0;
    broadcast_value = scalar;
    if (operand_select == 2)
      broadcast_value = immediate_unsigned ? {59'b0, immediate} : {{59{immediate[4]}}, immediate};
    if (out_width <= 64) begin
      lane_mask = 64'hffffffffffffffff >> (64 - width_bits);
      for (int lane = 0; lane < 64 / out_width; lane++) begin
        source_lane = lane + ((widening && upper_half) ? lanes / 2 : 0);
        position = first + lane;
        enabled = position >= int'(vstart) && position < int'(vl) && position < int'(vlmax) &&
                  (!masked || m[position % 64]);
        x = (a >> (source_lane * width_bits)) & lane_mask;
        y = operand_select == 0 ? (b >> (source_lane * width_bits)) & lane_mask : broadcast_value & lane_mask;
        case (operation)
          0: value = x + y;
          1: value = x - y;
          2: value = x ^ y;
          3: value = {63'b0, x < y};
          4: value = x << int'(y & 64'(out_width - 1));
          default: value = x + y;
        endcase
        value &= 64'hffffffffffffffff >> (64 - out_width);
        if (enabled) begin
          if (mask_destination) begin
            if (x < y) data |= 64'b1 << (position % 64);
            mask_bits |= 64'b1 << (position % 64);
          end else begin
            data |= value << (lane * out_width);
            mask_bits |= (64'hffffffffffffffff >> (64 - out_width)) << (lane * out_width);
          end
        end
      end
    end
    expected_valid = read_valid && !reset;
    @(posedge clock); #1;
    assert (result_valid[bank_index] == expected_valid) else $fatal(1, "read latency/reset vlen=%0d", vlen_bits);
    if (expected_valid) begin
      assert (read_a_result[bank_index*64 +: 64] == a &&
              read_b_result[bank_index*64 +: 64] == b &&
              read_c_result[bank_index*64 +: 64] == c)
        else $fatal(1, "read/forwarding vlen=%0d", vlen_bits);
      assert (result_legal[bank_index] == legal) else $fatal(1, "packing legality vlen=%0d first=%0d width=%0d", vlen_bits, first_element, width_bits);
      if (legal) begin
        actual_data = result_data[bank_index*64 +: 64];
        actual_mask = result_mask[bank_index*64 +: 64];
        assert (int'(result_address[bank_index*16 +: 16]) == address &&
                actual_data == data && actual_mask == mask_bits)
          else $fatal(1, "packed result vlen=%0d width=%0d first=%0d op=%0d got=%h/%h expected=%h/%h addr=%0d/%0d",
                      vlen_bits, width_bits, first, operation, actual_data, actual_mask, data, mask_bits,
                      result_address[bank_index*16 +: 16], address);
      end
      checks++;
    end
    pending_data = data; pending_mask = mask_bits; pending_address = address;
    pending_reads[0] = a; pending_reads[1] = b; pending_reads[2] = c;
    pending_valid = expected_valid; pending_legal = legal;
    @(negedge clock);
  endtask

  task automatic defaults;
    read_valid = 0; write_valid = 0; commit = 0;
    read_a = 0; read_b = 0; read_c = 0;
    write_address = 0; write_data = 0; write_mask = 0;
    destination = 0; mask_destination = 0; element_width = 0;
    first_element = 0; vl = 0; vstart = 0; vlmax = 1;
    masked = 0; operand_select = 0; immediate_unsigned = 0;
    widening = 0; upper_half = 0; scalar = 0; immediate = 0; operation = 0;
  endtask

  initial begin
    defaults();
    bank_index = 0; vlen_bits = 128; depth = 64; chunks = 2;
    pending_valid = 0; pending_legal = 0;
    for (int i = 0; i < 256; i++) memory[i] = 0;
    step(); reset = 0;
    for (int config_index = 0; config_index < 3; config_index++) begin
      bank_index = config_index; vlen_bits = 128 << config_index;
      pending_valid = 0;
      chunks = vlen_bits / 64; depth = 32 * chunks;
      defaults();
      // Initialize every physical chunk, including writable v0 and v31.
      for (int address = 0; address < depth; address++) begin
        write_valid = 1; write_address = 16'(address);
        write_data = random_word(); write_mask = '1;
        step();
      end
      write_valid = 0;
      read_valid = 1; vlmax = 17'(vlen_bits); vl = 17'(vlen_bits);
      for (int address = 0; address < depth; address++) begin
        read_a = 16'(address); read_b = 16'((address + 1) % depth);
        read_c = 16'((address + depth - 1) % depth);
        step();
      end
      // All three ports collide with masked writes; disjoint updates accumulate.
      read_a = 0; read_b = 0; read_c = 0;
      write_address = 0; write_valid = 1;
      for (int bit_index = 0; bit_index < 64; bit_index++) begin
        write_mask = 64'b1 << bit_index; write_data = random_word();
        step();
      end
      write_valid = 0;
      // Read-before-write snapshots make in-place vector operations safe.
      // Contiguous chunks cross a register boundary in an LMUL=2 group.
      for (int width_index = 0; width_index < 4; width_index++) begin
        element_width = 2'(width_index); vlmax = 17'(2 * vlen_bits / (8 << width_index));
        vl = vlmax - 1; vstart = 1; destination = 4; commit = 1;
        for (int chunk = 0; chunk < 2 * chunks; chunk++) begin
          first_element = 17'(chunk * (8 >> width_index));
          read_a = 16'(4 * chunks + chunk); read_b = 16'(8 * chunks + chunk);
          read_c = 16'(first_element / 64);
          step();
        end
        read_valid = 0; step(); commit = 0; read_valid = 1;
      end
      // Legal widening overlap: the narrow source is the upper register of
      // the destination group. Ascending source chunks preserve unread data.
      element_width = 0; widening = 1; masked = 0; operation = 0;
      vlmax = 17'(vlen_bits / 8); vl = vlmax - 1; vstart = 1;
      destination = 4; commit = 1;
      for (int chunk = 0; chunk < chunks; chunk++) begin
        first_element = 17'(chunk * 8);
        read_a = 16'(5 * chunks + chunk); read_b = 16'(8 * chunks + chunk);
        upper_half = 0; step(); upper_half = 1; step();
      end
      read_valid = 0; step(); commit = 0; read_valid = 1; widening = 0;
      // Compare bits in successive mask chunks without overwriting neighbors.
      element_width = 0; operation = 3; mask_destination = 1;
      masked = 1; vlmax = 17'(vlen_bits); vl = vlmax - 3; vstart = 5;
      destination = 31; commit = 1;
      for (int element = 0; element < vlen_bits; element += 8) begin
        first_element = 17'(element);
        read_a = 16'(8 * chunks + element / 8); read_b = 16'(16 * chunks + element / 8);
        read_c = 16'(element / 64);
        step();
      end
      read_valid = 0; step(); commit = 0; read_valid = 1;
      // A fully masked body emits no writes, even when explicitly committed.
      write_valid = 1; write_address = 0; write_data = 0; write_mask = '1;
      read_c = 0; first_element = 0; vstart = 0; vl = vlmax;
      step();
      assert (result_mask[bank_index*64 +: 64] == 0) else $fatal(1, "all-masked body wrote bits");
      write_valid = 0; commit = 1; step(); commit = 0;
      // Fractional LMUL, empty body, all masked, broadcasts, widening halves.
      for (int trial = 0; trial < 1600; trial++) begin
        element_width = 2'(trial % 4);
        widening = trial % 5 == 0 && element_width != 3;
        upper_half = (trial & 1) != 0;
        vlmax = 17'(vlen_bits / (8 << element_width));
        if (trial % 7 == 0 && element_width == 0) vlmax = 17'(vlen_bits / 64);
        first_element = 17'((trial % int'(vlmax)) / (8 >> element_width) * (8 >> element_width));
        vl = 17'(trial % (int'(vlmax) + 1));
        vstart = 17'((trial * 3) % (int'(vlmax) + 1));
        masked = (trial & 2) != 0; mask_destination = (trial % 9) == 0;
        operand_select = 2'(trial % 3); immediate_unsigned = (trial & 4) != 0;
        immediate = 5'(trial); scalar = random_word(); operation = 3'(trial % 5);
        destination = 5'(trial % 24);
        read_a = 16'(trial % depth); read_b = 16'((trial * 13) % depth);
        read_c = 16'((trial * 7) % chunks);
        commit = (trial % 3) != 0; read_valid = (trial % 17) != 0;
        write_valid = !commit; write_address = read_a; write_mask = random_word(); write_data = random_word();
        step();
      end
      // Reset cancels a read result and blocks writes without clearing storage.
      write_valid = 1; commit = 0; read_valid = 1;
      write_address = 0; write_mask = '1; write_data = 0; reset = 1; step();
      reset = 0; write_valid = 0; read_a = 0; step();
      // Invalid chunk alignment, unsupported widening, and bank overflow.
      defaults(); read_valid = 1; vlmax = 17'(vlen_bits); vl = vlmax;
      first_element = 1; step();
      first_element = 0; element_width = 3; widening = 1; step();
      widening = 0; destination = 31; first_element = 17'(vlen_bits / 64); step();
      $display("vector bank VLEN=%0d depth=%0d passed", vlen_bits, depth);
    end
    $display("vector storage/packing/SIMD passed %0d transactions", checks);
    $finish;
  end
endmodule
