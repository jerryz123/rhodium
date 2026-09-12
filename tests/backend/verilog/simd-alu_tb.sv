// Checks shared SIMD bit operations and widening against independent per-element models.
module simd_alu_tb;
  localparam logic [2:0] ADDER = 0, LOGIC_OP = 1, SHIFT = 2,
                         COMPARE = 3, MINMAX = 4, SELECT_OP = 5, PERMUTE = 6, COUNT = 7;
  logic [63:0] left, right, data;
  logic [1:0] element_width, logic_select, comparison_select;
  logic [1:0] permutation_select, count_select, widen_element_width, prepared_width;
  logic [2:0] result_select;
  logic subtract, signed_compare, maximum, shift_right, arithmetic_shift;
  logic rotate, invert_right, widening, upper_half;
  logic [63:0] prepared_left, prepared_right;
  logic [7:0] prepared_enabled;
  logic [7:0] enabled, select_right, comparison, write_mask;
  longint unsigned checks = 0;
  longint unsigned rng = 64'h9e3779b97f4a7c15;

  SimdAluFixture dut (.*);

  function automatic logic [63:0] random_word();
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
  endfunction

  task automatic check_result;
    int width_bits, lane_count, amount, count;
    logic [63:0] mask, a, b, logic_b, value, expected_data;
    logic signed [63:0] signed_a, signed_b;
    logic lt, eq, predicate;
    logic [7:0] expected_comparison, expected_write_mask;
    width_bits = 8 << element_width;
    lane_count = 64 / width_bits;
    mask = 64'hffffffffffffffff >> (64 - width_bits);
    expected_data = 0;
    expected_comparison = 0;
    expected_write_mask = 0;
    for (int lane = 0; lane < lane_count; lane++) begin
      a = (left >> (lane * width_bits)) & mask;
      b = (right >> (lane * width_bits)) & mask;
      signed_a = $signed(a << (64 - width_bits)) >>> (64 - width_bits);
      signed_b = $signed(b << (64 - width_bits)) >>> (64 - width_bits);
      lt = signed_compare ? signed_a < signed_b : a < b;
      eq = a == b;
      case (comparison_select)
        0: predicate = eq;
        1: predicate = lt;
        2: predicate = lt || eq;
        default: $fatal(1, "invalid test comparison");
      endcase
      amount = int'(b & 64'(width_bits - 1));
      logic_b = invert_right ? ~b : b;
      case (result_select)
        ADDER: value = subtract ? a - b : a + b;
        LOGIC_OP: begin
          case (logic_select)
            0: value = a & logic_b;
            1: value = a | logic_b;
            2: value = a ^ logic_b;
            default: $fatal(1, "invalid test logic operation");
          endcase
        end
        SHIFT: begin
          if (rotate) begin
            // Reference permutations avoid the implementation's barrel stages.
            value = 0;
            for (int bit_index = 0; bit_index < width_bits; bit_index++)
              value[bit_index] = a[(bit_index + (shift_right ? amount : width_bits - amount)) % width_bits];
          end
          else if (!shift_right) value = a << amount;
          else if (arithmetic_shift) value = 64'(signed_a >>> amount);
          else value = a >> amount;
        end
        COMPARE: value = {63'b0, predicate};
        MINMAX: value = maximum ? (lt ? b : a) : (lt ? a : b);
        SELECT_OP: value = select_right[lane] ? b : a;
        PERMUTE: begin
          value = 0;
          for (int bit_index = 0; bit_index < width_bits; bit_index++) begin
            case (permutation_select)
              0: value[bit_index] = a[width_bits - 1 - bit_index];
              1: value[bit_index] = a[(bit_index / 8) * 8 + 7 - bit_index % 8];
              2: value[bit_index] = a[(width_bits / 8 - 1 - bit_index / 8) * 8 + bit_index % 8];
              default: $fatal(1, "invalid test permutation");
            endcase
          end
        end
        COUNT: begin
          count = 0;
          if (count_select == 2) begin
            for (int bit_index = 0; bit_index < width_bits; bit_index++) count += int'(a[bit_index]);
          end else begin
            for (int bit_index = 0; bit_index < width_bits; bit_index++) begin
              if (a[count_select == 0 ? width_bits - 1 - bit_index : bit_index]) break;
              count++;
            end
          end
          value = 64'(count);
        end
        default: $fatal(1, "invalid test result selector");
      endcase
      if (enabled[lane]) begin
        expected_data |= (value & mask) << (lane * width_bits);
        expected_comparison[lane] = predicate;
        for (int byte_index = 0; byte_index < width_bits / 8; byte_index++)
          expected_write_mask[lane * (width_bits / 8) + byte_index] = 1;
      end
    end
    #1;
    assert (data === expected_data && comparison === expected_comparison &&
            write_mask === expected_write_mask)
      else $fatal(1, "check %0d w=%0d op=%0d logic=%0d cmp=%0d perm=%0d count=%0d rot=%b inv=%b sub=%0b signed=%0b max=%0b sr=%0b ar=%0b en=%h sel=%h a=%h b=%h got=%h/%h/%h expected=%h/%h/%h",
                  checks, width_bits, result_select, logic_select, comparison_select,
                  permutation_select, count_select, rotate, invert_right,
                  subtract, signed_compare, maximum, shift_right, arithmetic_shift,
                  enabled, select_right, left, right, data, comparison, write_mask,
                  expected_data, expected_comparison, expected_write_mask);
    checks++;
  endtask

  task automatic exercise_operations;
    widening = 0;
    rotate = 0;
    invert_right = 0;
    result_select = ADDER;
    subtract = 0; check_result();
    subtract = 1; check_result();
    result_select = LOGIC_OP;
    for (int op = 0; op < 3; op++) begin
      logic_select = 2'(op);
      invert_right = 0;
      check_result();
      invert_right = 1;
      check_result();
    end
    invert_right = 0;
    for (int sign_mode = 0; sign_mode < 2; sign_mode++) begin
      signed_compare = 1'(sign_mode);
      result_select = COMPARE;
      for (int cmp = 0; cmp < 3; cmp++) begin
        comparison_select = 2'(cmp);
        check_result();
      end
      result_select = MINMAX;
      maximum = 0; check_result();
      maximum = 1; check_result();
    end
    result_select = SHIFT;
    for (int direction = 0; direction < 2; direction++) begin
      shift_right = 1'(direction);
      arithmetic_shift = 0; check_result();
      arithmetic_shift = 1; check_result();
      rotate = 1;
      // Rotation must ignore arithmetic fill, including negative elements.
      check_result();
      arithmetic_shift = 0; check_result();
      rotate = 0;
    end
    result_select = SELECT_OP;
    check_result();
    for (int op = 0; op < 3; op++) begin
      result_select = PERMUTE;
      permutation_select = 2'(op); check_result();
      result_select = COUNT;
      count_select = 2'(op); check_result();
    end
  endtask

  task automatic check_widen;
    int source_bits, destination_bits, lanes, source_lane, amount;
    logic [63:0] source_mask, destination_mask, a, b, expected_left, expected_right, expected_data;
    logic [7:0] expected_enabled, expected_write_mask;
    source_bits = 8 << widen_element_width;
    destination_bits = 2 * source_bits;
    lanes = 64 / destination_bits;
    source_mask = (64'h1 << source_bits) - 1;
    destination_mask = '1;
    destination_mask >>= 64 - destination_bits;
    expected_left = 0; expected_right = 0; expected_data = 0;
    expected_enabled = 0; expected_write_mask = 0;
    for (int lane = 0; lane < lanes; lane++) begin
      source_lane = lane + (upper_half ? lanes : 0);
      a = (left >> (source_lane * source_bits)) & source_mask;
      b = (right >> (source_lane * source_bits)) & source_mask;
      expected_left |= a << (lane * destination_bits);
      expected_right |= b << (lane * destination_bits);
      expected_enabled[lane] = enabled[source_lane];
      amount = int'(b & 64'(destination_bits - 1));
      if (enabled[source_lane]) begin
        expected_data |= ((a << amount) & destination_mask) << (lane * destination_bits);
        for (int byte_index = 0; byte_index < destination_bits / 8; byte_index++)
          expected_write_mask[lane * destination_bits / 8 + byte_index] = 1;
      end
    end
    widening = 1;
    result_select = SHIFT;
    rotate = 0; arithmetic_shift = 0; shift_right = 0;
    #1;
    assert (prepared_left === expected_left && prepared_right === expected_right &&
            prepared_width === widen_element_width + 2'd1 && prepared_enabled === expected_enabled &&
            data === expected_data && write_mask === expected_write_mask)
      else $fatal(1, "widen check %0d source=%0d upper=%b a=%h b=%h en=%h prep=%h/%h/%h/%h expected=%h/%h/%h result=%h/%h expected=%h/%h",
                  checks, source_bits, upper_half, left, right, enabled, prepared_left, prepared_right,
                  prepared_width, prepared_enabled, expected_left, expected_right, expected_enabled,
                  data, write_mask, expected_data, expected_write_mask);
    checks++;
  endtask

  initial begin
    left = 0;
    right = 0;
    element_width = 0;
    result_select = ADDER;
    logic_select = 0;
    comparison_select = 0;
    subtract = 0;
    signed_compare = 0;
    maximum = 0;
    shift_right = 0;
    arithmetic_shift = 0;
    rotate = 0;
    invert_right = 0;
    widening = 0;
    upper_half = 0;
    widen_element_width = 0;
    permutation_select = 0;
    count_select = 0;
    enabled = 8'hff;
    select_right = 8'haa;

    // Exhaust every pair of byte values. Neighboring lanes use different
    // operands, exposing carry/borrow and shift leakage between packed lanes.
    for (int a = 0; a < 256; a++) begin
      for (int b = 0; b < 256; b++) begin
        for (int lane = 0; lane < 8; lane++) begin
          left[lane * 8 +: 8] = 8'(a + lane * 37);
          right[lane * 8 +: 8] = 8'(b ^ (lane * 53));
        end
        exercise_operations();
      end
    end

    for (int size = 0; size < 4; size++) begin
      element_width = 2'(size);
      // Exercise every lane enable pattern, including unused high mask bits.
      for (int enables = 0; enables < 256; enables++) begin
        enabled = 8'(enables);
        select_right = ~8'(enables);
        left = 64'h80017fff80ff00ff;
        right = 64'h7fff80017f01ff01;
        exercise_operations();
      end
      // Boundary carries, full-word overflow/borrow, and signed extremes.
      enabled = 8'hff;
      for (int bit_index = 0; bit_index < 64; bit_index++) begin
        left = 64'h1 << bit_index;
        right = left - 1;
        exercise_operations();
        left = ~left;
        right = ~right;
        exercise_operations();
      end
      left = '1; right = 1; exercise_operations();
      left = 0; right = '1; exercise_operations();
      left = 64'h8000000000000000;
      right = 64'h7fffffffffffffff; exercise_operations();
      right = left; exercise_operations();
      left = 0; right = 0; exercise_operations();
      left = '1; right = '1; exercise_operations();
      // Test every low-byte shift pattern with dirty high operand bits.
      for (int amount = 0; amount < 256; amount++) begin
        left = 64'h8123456789abcdef;
        right = 64'hffffffffffffff00 | 64'(amount);
        exercise_operations();
      end
    end

    // Change widths and all controls without reset; this is a stateless unit.
    for (int trial = 0; trial < 20000; trial++) begin
      left = random_word();
      right = random_word();
      element_width = 2'(random_word());
      enabled = 8'(random_word());
      select_right = 8'(random_word());
      exercise_operations();
    end

    // Every source width and half, every mask, and every byte-sized amount.
    // In particular shifts by SEW must survive widening instead of becoming 0.
    for (int size = 0; size < 3; size++) begin
      widen_element_width = 2'(size);
      for (int half = 0; half < 2; half++) begin
        upper_half = 1'(half);
        for (int amount = 0; amount < 256; amount++) begin
          left = 64'hfedcba9876543210;
          right = 64'h0101010101010101 * 64'(amount);
          enabled = 8'(amount);
          check_widen();
          enabled = '1;
          check_widen();
        end
      end
    end
    for (int trial = 0; trial < 10000; trial++) begin
      left = random_word(); right = random_word(); enabled = 8'(random_word());
      widen_element_width = 2'(random_word() % 3);
      upper_half = 1'(random_word());
      check_widen();
    end
    $display("simd-alu PASS: %0d per-element differential checks", checks);
    $finish;
  end
endmodule
