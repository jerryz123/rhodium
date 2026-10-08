// Scores packed SIMD operations against independent per-element arithmetic
// models.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
using uint128 = unsigned __int128;
using int128 = __int128;
#ifndef SIMD_WIDTH
#define SIMD_WIDTH 64
#endif
constexpr int WIDTH = SIMD_WIDTH, BYTE_COUNT = WIDTH / 8,
              FORMATS = WIDTH == 64 ? 4 : 3;
constexpr int ADDER = 0, LOGIC_OP = 1, SHIFT = 2, COMPARE = 3, MINMAX = 4,
              SELECT_OP = 5, PERMUTE = 6, COUNT = 7;
constexpr int WRAP = 0, SATURATE_UNSIGNED = 1, SATURATE_SIGNED = 2,
              AVERAGE_UNSIGNED = 3, AVERAGE_SIGNED = 4;
std::uint64_t checks = 0, rng = UINT64_C(0x9e3779b97f4a7c15);
template <class T, class U>
void set_bits(T &dst, unsigned offset, unsigned width, U value) {
  const T mask = T(low_mask(width)) << offset;
  dst = (dst & ~mask) | ((T(value) << offset) & mask);
}
std::uint64_t random_word() {
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return rng;
}

void check_result() {
  int width_bits, lane_count, amount, count, compressed_elements;
  std::uint64_t mask, a, b, logic_b, value, expected_data, expected_compressed,
      discarded_mask;
  uint128 wide_result, wide_right, average_base;
  int128 exact_result, extended_a, extended_b;
  std::int64_t signed_a, signed_b;
  bool lt, eq, predicate, carry_bit, carry_borrow, round_bit, lower_nonzero,
      discarded_nonzero, increment, overflow, expected_saturated;
  std::uint8_t expected_mask_result, expected_write_mask;
  width_bits = 8 << element_width;
  lane_count = WIDTH / width_bits;
  mask = UINT64_C(18446744073709551615) >> (64 - width_bits);
  expected_data = 0;
  expected_mask_result = 0;
  expected_write_mask = 0;
  expected_saturated = 0;
  expected_compressed = 0;
  compressed_elements = 0;
  for (int lane = 0; lane < lane_count; lane++) {
    a = (left >> (lane * width_bits)) & mask;
    b = (right >> (lane * width_bits)) & mask;
    signed_a = std::int64_t(a << (64 - width_bits)) >> (64 - width_bits);
    signed_b = std::int64_t(b << (64 - width_bits)) >> (64 - width_bits);
    extended_a = int128(signed_a);
    extended_b = int128(signed_b);
    lt = signed_compare ? signed_a < signed_b : a < b;
    eq = a == b;
    carry_bit = ((carry_in >> (lane)) & 1);
    wide_result = uint128(a) + uint128(b) + uint128(carry_bit);
    wide_right = uint128(b) + uint128(carry_bit);
    carry_borrow = subtract ? uint128(a) < wide_right
                            : ((wide_result >> (width_bits)) & 1);
    switch (comparison_select) {
    case 0:
      predicate = eq;

      break;
    case 1:
      predicate = lt;

      break;
    case 2:
      predicate = lt || eq;

      break;
    default:
      fail(1, "invalid test comparison");

      break;
    }
    amount = int(b & ((width_bits - 1) & low_mask(64)));
    logic_b = invert_right ? ~b : b;
    switch (result_select) {
    case ADDER: {
      value = subtract ? a - b - ((carry_bit)&low_mask(64))
                       : a + b + ((carry_bit)&low_mask(64));
      if (arithmetic_mode == SATURATE_UNSIGNED) {
        overflow = carry_borrow;
        if (overflow)
          value = subtract ? 0 : mask;
        expected_saturated |= ((enabled >> (lane)) & 1) && overflow;
      } else if (arithmetic_mode == SATURATE_SIGNED) {
        overflow = (((a >> (width_bits - 1)) & 1) !=
                    ((value >> (width_bits - 1)) & 1)) &&
                   (subtract ? ((a >> (width_bits - 1)) & 1) !=
                                   ((b >> (width_bits - 1)) & 1)
                             : ((a >> (width_bits - 1)) & 1) ==
                                   ((b >> (width_bits - 1)) & 1));
        if (overflow)
          value = ((a >> (width_bits - 1)) & 1)
                      ? UINT64_C(1) << (width_bits - 1)
                      : (UINT64_C(1) << (width_bits - 1)) - 1;
        expected_saturated |= ((enabled >> (lane)) & 1) && overflow;
      } else if (arithmetic_mode == AVERAGE_UNSIGNED ||
                 arithmetic_mode == AVERAGE_SIGNED) {
        if (arithmetic_mode == AVERAGE_UNSIGNED && !subtract) {
          wide_result = uint128(a) + uint128(b);
          average_base = wide_result >> 1;
          round_bit = ((wide_result >> (0)) & 1);
        } else {
          exact_result =
              arithmetic_mode == AVERAGE_SIGNED ? extended_a : int128(a);
          exact_result = subtract
                             ? exact_result - (arithmetic_mode == AVERAGE_SIGNED
                                                   ? extended_b
                                                   : int128(b))
                             : exact_result + extended_b;
          average_base = exact_result >> 1;
          round_bit = ((exact_result >> (0)) & 1);
        }
        switch (rounding_mode) {
        case 0:
          increment = round_bit;

          break;
        case 1:
          increment = round_bit && ((average_base >> (0)) & 1);

          break;
        case 2:
          increment = 0;

          break;
        case 3:
          increment = !((average_base >> (0)) & 1) && round_bit;

          break;
        default:
          fail(1, "invalid averaging mode");

          break;
        }
        value = std::uint64_t(average_base) + ((increment)&low_mask(64));
      }
    }

    break;
    case LOGIC_OP: {
      switch (logic_select) {
      case 0:
        value = a & logic_b;

        break;
      case 1:
        value = a | logic_b;

        break;
      case 2:
        value = a ^ logic_b;

        break;
      default:
        fail(1, "invalid test bool operation");

        break;
      }
    }

    break;
    case SHIFT: {
      if (rotate) {

        value = 0;
        for (int bit_index = 0; bit_index < width_bits; bit_index++)
          set_bits(value, bit_index, 1,
                   ((a >> ((bit_index +
                            (shift_right ? amount : width_bits - amount)) %
                           width_bits)) &
                    1));
      } else if (!shift_right)
        value = a << amount;
      else if (arithmetic_shift)
        value = ((signed_a >> amount) & low_mask(64));
      else
        value = a >> amount;
      if (rounding) {
        round_bit = amount == 0 ? 0 : ((a >> (amount - 1)) & 1);
        discarded_mask = amount == 0 ? 0 : mask >> (width_bits - amount);
        discarded_nonzero = (a & discarded_mask) != 0;
        lower_nonzero = amount <= 1 ? 0 : (a & (discarded_mask >> 1)) != 0;
        switch (rounding_mode) {
        case 0:
          increment = round_bit;

          break;
        case 1:
          increment = round_bit && (lower_nonzero || ((value >> (0)) & 1));

          break;
        case 2:
          increment = 0;

          break;
        case 3:
          increment = !((value >> (0)) & 1) && discarded_nonzero;

          break;
        default:
          fail(1, "invalid rounding mode");

          break;
        }
        value += ((increment)&low_mask(64));
      }
    }

    break;
    case COMPARE:
      value = std::uint64_t(predicate);

      break;
    case MINMAX:
      value = maximum ? (lt ? b : a) : (lt ? a : b);

      break;
    case SELECT_OP:
      value = ((select_right >> (lane)) & 1) ? b : a;

      break;
    case PERMUTE: {
      value = 0;
      for (int bit_index = 0; bit_index < width_bits; bit_index++) {
        switch (permutation_select) {
        case 0:
          set_bits(value, bit_index, 1,
                   ((a >> (width_bits - 1 - bit_index)) & 1));

          break;
        case 1:
          set_bits(value, bit_index, 1,
                   ((a >> ((bit_index / 8) * 8 + 7 - bit_index % 8)) & 1));

          break;
        case 2:
          set_bits(value, bit_index, 1,
                   ((a >> ((width_bits / 8 - 1 - bit_index / 8) * 8 +
                           bit_index % 8)) &
                    1));

          break;
        default:
          fail(1, "invalid test permutation");

          break;
        }
      }
    }

    break;
    case COUNT: {
      count = 0;
      if (count_select == 2) {
        for (int bit_index = 0; bit_index < width_bits; bit_index++)
          count += int(((a >> (bit_index)) & 1));
      } else {
        for (int bit_index = 0; bit_index < width_bits; bit_index++) {
          if (((a >>
                (count_select == 0 ? width_bits - 1 - bit_index : bit_index)) &
               1))
            break;
          count++;
        }
      }
      value = ((count)&low_mask(64));
    }

    break;
    default:
      fail(1, "invalid test result selector");

      break;
    }
    if (((enabled >> (lane)) & 1)) {
      expected_compressed |= a << (compressed_elements * width_bits);
      compressed_elements++;
      expected_data |= (value & mask) << (lane * width_bits);
      set_bits(expected_mask_result, lane, 1,
               mask_result_select ? carry_borrow : predicate);
      for (int byte_index = 0; byte_index < width_bits / 8; byte_index++)
        set_bits(expected_write_mask, lane * (width_bits / 8) + byte_index, 1,
                 1);
    }
  }
  eval();
  CHECK(data == expected_data && mask_result == expected_mask_result &&
        write_mask == expected_write_mask && saturated == expected_saturated);
  CHECK(compressed_data == expected_compressed &&
        compressed_count == ((compressed_elements)&low_mask(4)));
  checks++;
}

void exercise_operations() {
  carry_in = 0;
  mask_result_select = 0;
  widening = 0;
  rounding = 0;
  arithmetic_mode = WRAP;
  rotate = 0;
  invert_right = 0;
  result_select = ADDER;
  subtract = 0;
  check_result();
  subtract = 1;
  check_result();
  result_select = LOGIC_OP;
  for (int op = 0; op < 3; op++) {
    logic_select = ((op)&low_mask(2));
    invert_right = 0;
    check_result();
    invert_right = 1;
    check_result();
  }
  invert_right = 0;
  for (int sign_mode = 0; sign_mode < 2; sign_mode++) {
    signed_compare = ((sign_mode)&low_mask(1));
    result_select = COMPARE;
    for (int cmp = 0; cmp < 3; cmp++) {
      comparison_select = ((cmp)&low_mask(2));
      check_result();
    }
    result_select = MINMAX;
    maximum = 0;
    check_result();
    maximum = 1;
    check_result();
  }
  result_select = SHIFT;
  for (int direction = 0; direction < 2; direction++) {
    shift_right = ((direction)&low_mask(1));
    arithmetic_shift = 0;
    check_result();
    arithmetic_shift = 1;
    check_result();
    rotate = 1;

    check_result();
    arithmetic_shift = 0;
    check_result();
    rotate = 0;
  }
  result_select = SELECT_OP;
  check_result();
  for (int op = 0; op < 3; op++) {
    result_select = PERMUTE;
    permutation_select = ((op)&low_mask(2));
    check_result();
    result_select = COUNT;
    count_select = ((op)&low_mask(2));
    check_result();
  }
}

void exercise_carry_borrow() {
  widening = 0;
  rounding = 0;
  arithmetic_mode = WRAP;
  result_select = ADDER;
  mask_result_select = 1;
  subtract = 0;
  check_result();
  subtract = 1;
  check_result();
  mask_result_select = 0;
  carry_in = 0;
}

void check_widen() {
  int source_bits, destination_bits, lanes, source_lane, amount;
  std::uint64_t source_mask, destination_mask, a, b, extended_a, extended_b,
      expected_left, expected_right, expected_data;
  std::uint8_t expected_enabled, expected_write_mask;
  source_bits = 8 << widen_element_width;
  destination_bits = 2 * source_bits;
  lanes = WIDTH / destination_bits;
  source_mask = (UINT64_C(1) << source_bits) - 1;
  destination_mask = UINT64_MAX;
  destination_mask >>= 64 - destination_bits;
  expected_left = 0;
  expected_right = 0;
  expected_data = 0;
  expected_enabled = 0;
  expected_write_mask = 0;
  for (int lane = 0; lane < lanes; lane++) {
    source_lane = lane + (upper_half ? lanes : 0);
    a = widen_left_wide ? (left >> (lane * destination_bits)) & destination_mask
                        : (left >> (source_lane * source_bits)) & source_mask;
    b = (right >> (source_lane * source_bits)) & source_mask;
    extended_a = widen_left_wide ? a
                 : widen_left_signed && ((a >> (source_bits - 1)) & 1)
                     ? a | ~source_mask
                     : a;
    extended_b = widen_right_signed && ((b >> (source_bits - 1)) & 1)
                     ? b | ~source_mask
                     : b;
    expected_left |= (extended_a & destination_mask)
                     << (lane * destination_bits);
    expected_right |= (extended_b & destination_mask)
                      << (lane * destination_bits);
    set_bits(expected_enabled, lane, 1, ((enabled >> (source_lane)) & 1));
    amount = int(extended_b & ((destination_bits - 1) & low_mask(64)));
    if (((enabled >> (source_lane)) & 1)) {
      expected_data |= ((extended_a << amount) & destination_mask)
                       << (lane * destination_bits);
      for (int byte_index = 0; byte_index < destination_bits / 8; byte_index++)
        set_bits(expected_write_mask, lane * destination_bits / 8 + byte_index,
                 1, 1);
    }
  }
  widening = 1;
  rounding = 0;
  result_select = SHIFT;
  rotate = 0;
  arithmetic_shift = 0;
  shift_right = 0;
  eval();
  CHECK(prepared_left == expected_left && prepared_right == expected_right &&
        prepared_width == widen_element_width + UINT64_C(1) &&
        prepared_enabled == expected_enabled && data == expected_data &&
        write_mask == expected_write_mask);
  checks++;
}

void check_extend() {
  int ratio_value, destination_bits, source_bits, lanes, source_offset;
  bool expected_legal;
  std::uint64_t source_mask, destination_mask, value, expected;
  ratio_value = 2 << extension_ratio;
  destination_bits = 8 << element_width;
  source_bits = destination_bits / ratio_value;
  expected_legal = destination_bits <= WIDTH &&
                   element_width >= extension_ratio + 1 &&
                   int(extension_part) < ratio_value;
  eval();
  CHECK(extension_legal == expected_legal);
  if (expected_legal) {
    lanes = WIDTH / destination_bits;
    source_offset = int(extension_part) * (WIDTH / ratio_value);
    source_mask = UINT64_MAX >> (64 - source_bits);
    destination_mask = UINT64_MAX >> (64 - destination_bits);
    expected = 0;
    for (int lane = 0; lane < lanes; lane++) {
      value = (left >> (source_offset + lane * source_bits)) & source_mask;
      if (extension_signed && ((value >> (source_bits - 1)) & 1))
        value |= ~source_mask;
      expected |= (value & destination_mask) << (lane * destination_bits);
    }
    CHECK(extension_data == expected);
  }
  checks++;
}

int main() {
  return run_test([] {
    left = 0;
    right = 0;
    element_width = 0;
    result_select = ADDER;
    logic_select = 0;
    comparison_select = 0;
    mask_result_select = 0;
    subtract = 0;
    signed_compare = 0;
    maximum = 0;
    shift_right = 0;
    arithmetic_shift = 0;
    rotate = 0;
    invert_right = 0;
    rounding = 0;
    rounding_mode = 0;
    arithmetic_mode = WRAP;
    widening = 0;
    widen_left_wide = 0;
    widen_left_signed = 0;
    widen_right_signed = 0;
    upper_half = 0;
    widen_element_width = 0;
    extension_ratio = 0;
    extension_part = 0;
    extension_signed = 0;
    permutation_select = 0;
    count_select = 0;
    enabled = UINT64_C(255);
    carry_in = 0;
    select_right = UINT64_C(170);

    for (int a = 0; a < 256; a++) {
      for (int b = 0; b < 256; b++) {
        for (int lane = 0; lane < BYTE_COUNT; lane++) {
          set_bits(left, lane * 8, 8, ((a + lane * 37) & low_mask(8)));
          set_bits(right, lane * 8, 8, ((b ^ (lane * 53)) & low_mask(8)));
        }
        exercise_operations();
        carry_in = UINT64_C(165);
        exercise_carry_borrow();
      }
    }

    for (int size = 0; size < FORMATS; size++) {
      element_width = ((size)&low_mask(2));

      for (int enables = 0; enables < 256; enables++) {
        enabled = ((enables)&low_mask(8));
        select_right = ~((enables)&low_mask(8));
        left = UINT64_C(9223794247189070079);
        right = UINT64_C(9223231305792225025);
        exercise_operations();
      }

      enabled = UINT64_C(255);
      for (int bit_index = 0; bit_index < WIDTH; bit_index++) {
        left = UINT64_C(1) << bit_index;
        right = left - 1;
        exercise_operations();
        left = ~left;
        right = ~right;
        exercise_operations();
      }
      left = UINT64_MAX;
      right = 1;
      exercise_operations();
      left = 0;
      right = UINT64_MAX;
      exercise_operations();
      left = UINT64_C(1) << (WIDTH - 1);
      right = left - 1;
      exercise_operations();
      right = left;
      exercise_operations();
      left = 0;
      right = 0;
      exercise_operations();
      left = UINT64_MAX;
      right = UINT64_MAX;
      exercise_operations();
      for (int trial = 0; trial < 256; trial++) {
        left = random_word();
        right = random_word();
        enabled = ((random_word()) & low_mask(8));
        carry_in = ((random_word()) & low_mask(8));
        exercise_carry_borrow();
      }

      for (int amount = 0; amount < 256; amount++) {
        left = UINT64_C(9305357566071262703);
        right = UINT64_C(18446744073709551360) | ((amount)&low_mask(64));
        exercise_operations();
      }

      result_select = SHIFT;
      shift_right = 1;
      rotate = 0;
      rounding = 1;
      for (int arithmetic = 0; arithmetic < 2; arithmetic++) {
        arithmetic_shift = ((arithmetic)&low_mask(1));
        for (int mode = 0; mode < 4; mode++) {
          rounding_mode = ((mode)&low_mask(2));
          for (int amount = 0; amount < (8 << size); amount++) {
            left = random_word();
            right = 0;
            for (int lane = 0; lane < (BYTE_COUNT >> size); lane++)
              right |= ((amount)&low_mask(64)) << (lane * (8 << size));
            enabled = ((random_word()) & low_mask(8));
            check_result();
          }
        }
      }
      rounding = 0;

      result_select = ADDER;
      for (int fixed_mode = int(SATURATE_UNSIGNED);
           fixed_mode <= int(AVERAGE_SIGNED); fixed_mode++) {
        arithmetic_mode = ((fixed_mode)&low_mask(3));
        for (int direction = 0; direction < 2; direction++) {
          subtract = ((direction)&low_mask(1));
          for (int mode = 0; mode < 4; mode++) {
            rounding_mode = ((mode)&low_mask(2));
            for (int trial = 0; trial < 256; trial++) {
              left = random_word();
              right = random_word();
              enabled = ((random_word()) & low_mask(8));
              check_result();
            }
          }
        }
      }
      arithmetic_mode = WRAP;
    }

    for (int trial = 0; trial < 20000; trial++) {
      left = random_word();
      right = random_word();
      element_width =
          ((random_word() % ((FORMATS)&low_mask(64))) & low_mask(2));
      enabled = ((random_word()) & low_mask(8));
      select_right = ((random_word()) & low_mask(8));
      exercise_operations();
    }

    for (int size = 0; size < FORMATS - 1; size++) {
      widen_element_width = ((size)&low_mask(2));
      for (int half = 0; half < 2; half++) {
        upper_half = ((half)&low_mask(1));
        for (int amount = 0; amount < 256; amount++) {
          left = UINT64_C(18364758544493064720);
          right = UINT64_C(72340172838076673) * ((amount)&low_mask(64));
          enabled = ((amount)&low_mask(8));
          for (int source_form = 0; source_form < 2; source_form++) {
            widen_left_wide = ((source_form >> (0)) & 1);
            for (int signedness = 0; signedness < 4; signedness++) {
              widen_left_signed = ((signedness >> (0)) & 1);
              widen_right_signed = ((signedness >> (1)) & 1);
              check_widen();
            }
          }
          enabled = UINT64_MAX;
          check_widen();
        }
      }
    }
    for (int trial = 0; trial < 10000; trial++) {
      left = random_word();
      right = random_word();
      enabled = ((random_word()) & low_mask(8));
      widen_element_width =
          ((random_word() % ((FORMATS - 1) & low_mask(64))) & low_mask(2));
      upper_half = ((random_word()) & low_mask(1));
      widen_left_wide = ((random_word()) & low_mask(1));
      widen_left_signed = ((random_word()) & low_mask(1));
      widen_right_signed = ((random_word()) & low_mask(1));
      check_widen();
    }

    for (int ratio_index = 0; ratio_index < 3; ratio_index++) {
      extension_ratio = ((ratio_index)&low_mask(2));
      for (int width = 0; width < 4; width++) {
        element_width = ((width)&low_mask(2));
        for (int part = 0; part < 8; part++) {
          extension_part = ((part)&low_mask(3));
          for (int signedness = 0; signedness < 2; signedness++) {
            extension_signed = ((signedness)&low_mask(1));
            left = random_word();
            check_extend();
          }
        }
      }
    }
  });
}
