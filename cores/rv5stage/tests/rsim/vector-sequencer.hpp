// Models vector elements, fixed-point results, replay, and cancellation independently.
// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "test.hpp"
#include <bit>
using word_t = std::remove_cvref_t<decltype(scalar)>;
using uint128 = unsigned __int128;
using signed128 = __int128;
constexpr int CW = std::bit_width(unsigned(VLEN));
constexpr int DEPTH = 32 * VLEN / XLEN;
constexpr int AW = std::bit_width(unsigned(DEPTH - 1));
constexpr int SEW_LOG = std::countr_zero(unsigned(XLEN / 8));
template <class T> bool bit(T value, unsigned index) {
  return (value >> index) & 1;
}
template <class T> std::uint64_t bits(T value, unsigned hi, unsigned lo) {
  return std::uint64_t(value >> lo) & low_mask(hi - lo + 1);
}
std::int64_t sign_extend(std::uint64_t value, unsigned width) {
  value &= low_mask(width);
  if (width < 64 && bit(value, width - 1))
    value |= ~low_mask(width);
  return std::bit_cast<std::int64_t>(value);
}
void initialize(unsigned address, std::uint64_t data, std::uint64_t mask) {
  initialize_in.pbits.paddress = address;
  initialize_in.pbits.pdata = word_t(data);
  initialize_in.pbits.pmask = word_t(mask);
}
std::array<word_t, DEPTH> memory{}, snapshot{};
std::uint64_t rng = UINT64_C(0x713bfd9167c282c9);
int tx_source_width, tx_width, tx_lanes, tx_vl, tx_start, tx_first, tx_opcode,
    tx_mode, tx_vd, tx_vs1, tx_vs2, tx_vxrm;
std::uint64_t tx_scalar, tx_distance;
int tx_vlmax;
bool tx_masked, tx_compare, tx_extension, tx_extension_signed, tx_carry_family,
    tx_carry_input, tx_mask_logic, tx_dense, tx_gather, tx_gather_vector,
    tx_compress, tx_whole_move, tx_widening, tx_narrowing, tx_rounding,
    tx_saturating, tx_average, tx_clip, tx_clip_unsigned, tx_wide_source,
    tx_widen_signed, checking;
int tx_extension_ratio;
uint128 tx_compress_buffer;
int tx_compress_count, tx_compress_destination, tx_beats;
int checks = 0, macros = 0, retries = 0, cycles = 0, last_commit_cycle,
    consecutive = 0;
std::uint64_t random_word() {
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return rng;
}
std::uint64_t element(int regno, int index, int width_bits) {
  return (std::uint64_t(
              snapshot[regno * VLEN / XLEN + index * width_bits / XLEN]) >>
          (index * width_bits % XLEN)) &
         (UINT64_C(0xffffffffffffffff) >> (64 - width_bits));
}
std::int64_t signed_element(std::uint64_t value, int width_bits) {
  return sign_extend(value, width_bits);
}
std::uint64_t rounded_shift(std::uint64_t value, int width_bits,
                            std::uint64_t raw_amount, bool arithmetic,
                            std::uint8_t mode) {
  std::uint64_t mask, discarded_mask, shifted;
  bool round_bit, lower_nonzero, discarded_nonzero, increment;
  int amount;
  mask = UINT64_MAX >> (64 - width_bits);
  amount = int(raw_amount & std::uint64_t(width_bits - 1));
  shifted = arithmetic
                ? std::uint64_t(signed_element(value, width_bits) >> amount)
                : value >> amount;
  round_bit = amount == 0 ? 0 : bit(value, amount - 1);
  discarded_mask = amount == 0 ? 0 : mask >> (width_bits - amount);
  discarded_nonzero = (value & discarded_mask) != 0;
  lower_nonzero = amount <= 1 ? 0 : (value & (discarded_mask >> 1)) != 0;
  switch (mode) {
  case 0:
    increment = round_bit;
    break;
  case 1:
    increment = round_bit && (lower_nonzero || bit(shifted, 0));
    break;
  case 2:
    increment = 0;
    break;
  case 3:
    increment = !bit(shifted, 0) && discarded_nonzero;
    break;
  }
  return (shifted + std::uint64_t(increment)) & mask;
}
std::uint64_t rounded_average(std::uint64_t a, std::uint64_t b, int width_bits,
                              bool signed_operation, bool subtract,
                              std::uint8_t mode) {
  std::uint64_t mask;
  uint128 wide, base;
  std::int64_t signed_a, signed_b;
  signed128 exact, extended_a, extended_b;
  bool discarded, increment;
  mask = UINT64_MAX >> (64 - width_bits);
  signed_a = sign_extend(a, width_bits);
  signed_b = sign_extend(b, width_bits);
  extended_a = signed128(signed_a);
  extended_b = signed128(signed_b);
  if (!signed_operation && !subtract) {
    wide = uint128(a) + uint128(b);
    base = wide >> 1;
    discarded = bit(wide, 0);
  } else {
    exact = signed_operation ? extended_a : signed128(a);
    exact = subtract ? exact - (signed_operation ? extended_b : signed128(b))
                     : exact + extended_b;
    base = exact >> 1;
    discarded = bit(exact, 0);
  }
  switch (mode) {
  case 0:
    increment = discarded;
    break;
  case 1:
    increment = discarded && bit(base, 0);
    break;
  case 2:
    increment = 0;
    break;
  case 3:
    increment = !bit(base, 0) && discarded;
    break;
  }
  return (bits(base, 63, 0) + std::uint64_t(increment)) & mask;
}
void tick() {
  std::uint64_t a, b, value, lane_mask, expected_data, expected_mask,
      broadcast_value;
  uint128 wide_result;
  int first, ending, address, bit_offset, position, emitted, left_width;
  bool enabled, carry_input, was_retry, expected_saturated, overflow,
      result_negative, low_negative;
  eval();
  was_retry = retried;
  if (!reset) {
    if (initialize_in.pvalid)
      memory[initialize_in.pbits.paddress] =
          (memory[initialize_in.pbits.paddress] & ~initialize_in.pbits.pmask) |
          (initialize_in.pbits.pdata & initialize_in.pbits.pmask);
    if (retried)
      retries++;
    if (committed) {
      CHECK(checking);
      first = tx_first;
      ending = tx_start >= tx_vl
                   ? tx_vl
                   : ((first + tx_lanes < tx_vl) ? first + tx_lanes : tx_vl);
      address = tx_vd * VLEN / XLEN + (tx_compare || tx_mask_logic
                                           ? first / XLEN
                                           : first * tx_width / XLEN);
      expected_data = 0;
      expected_mask = 0;
      expected_saturated = 0;
      lane_mask = UINT64_C(0xffffffffffffffff) >> (64 - tx_width);
      broadcast_value = tx_scalar;
      if (tx_mode == 3)
        broadcast_value = (tx_opcode >= 37 && tx_opcode <= 47)
                              ? std::uint64_t(tx_vs1)
                              : std::uint64_t(sign_extend(tx_vs1, 5));
      if (tx_compress) {
        if (first < tx_vl) {
          for (int lane = 0; lane < tx_lanes && first + lane < ending; lane++) {
            position = first + lane;
            if (bit(snapshot[tx_vs1 * VLEN / XLEN + position / XLEN],
                    position % XLEN)) {
              tx_compress_buffer |= uint128(element(tx_vs2, position, tx_width))
                                    << (tx_compress_count * tx_width);
              tx_compress_count++;
            }
          }
        }
        emitted = tx_compress_count >= tx_lanes ? tx_lanes
                  : ending == tx_vl             ? tx_compress_count
                                                : 0;
        for (int lane = 0; lane < emitted; lane++)
          expected_mask |= lane_mask << (lane * tx_width);
        expected_data = std::uint64_t(bits(tx_compress_buffer, XLEN - 1, 0)) &
                        expected_mask;
        address =
            tx_vd * VLEN / XLEN + tx_compress_destination * tx_width / XLEN;
        tx_compress_buffer >>= emitted * tx_width;
        tx_compress_count -= emitted;
        tx_compress_destination += emitted;
      } else
        for (int lane = 0; lane < tx_lanes; lane++) {
          position = first + lane;
          enabled = position >= tx_start && position < tx_vl &&
                    (!tx_masked || tx_opcode == 23 || tx_carry_family ||
                     bit(snapshot[position / XLEN], position % XLEN));
          if (tx_opcode == 14 && tx_mode != 6 && !tx_gather &&
              std::uint64_t(position) < tx_distance)
            enabled = 0;
          if (enabled) {
            left_width = tx_narrowing     ? 2 * tx_width
                         : tx_wide_source ? tx_width
                                          : tx_source_width;
            a = element(tx_vs2, position, left_width);
            b = (tx_mode == 0 || tx_mode == 2) || tx_mask_logic
                    ? element(tx_vs1, position, tx_source_width)
                    : broadcast_value & (UINT64_C(0xffffffffffffffff) >>
                                         (64 - tx_source_width));
            if (tx_widen_signed) {
              a = signed_element(a,
                                 tx_wide_source ? tx_width : tx_source_width);
              b = signed_element(b, tx_source_width);
            }
            if (tx_extension) {
              value =
                  tx_extension_signed ? signed_element(a, tx_source_width) : a;
            } else if (tx_carry_family) {
              carry_input = tx_carry_input &&
                            bit(snapshot[position / XLEN], position % XLEN);
              if (!bit(tx_opcode, 1)) {
                wide_result = uint128(a) + uint128(b) + uint128(carry_input);
                value = bit(tx_opcode, 0)
                            ? std::uint64_t(bit(wide_result, tx_width))
                            : bits(wide_result, 63, 0);
              } else {
                wide_result = uint128(b) + uint128(carry_input);
                value = bit(tx_opcode, 0)
                            ? std::uint64_t(uint128(a) < wide_result)
                            : a - b - std::uint64_t(carry_input);
              }
            } else if (tx_mask_logic) {
              switch (tx_opcode) {
              case 24:
                value = a & ~b;
                break;
              case 25:
                value = a & b;
                break;
              case 26:
                value = a | b;
                break;
              case 27:
                value = a ^ b;
                break;
              case 28:
                value = a | ~b;
                break;
              case 29:
                value = ~(a & b);
                break;
              case 30:
                value = ~(a | b);
                break;
              case 31:
                value = ~(a ^ b);
                break;
              default:
                fail(1, "bad mask opcode");
                break;
              }
            } else if (tx_gather) {
              b = tx_gather_vector ? element(tx_vs1, position,
                                             tx_opcode == 14 ? 16 : tx_width)
                                   : tx_distance;
              value = b >= std::uint64_t(tx_vlmax)
                          ? 0
                          : element(tx_vs2, int(b), tx_width);
            } else if (tx_narrowing) {
              value = tx_rounding
                          ? rounded_shift(a, left_width, b, bit(tx_opcode, 0),
                                          ((tx_vxrm)&low_mask(2)))
                          : (bit(tx_opcode, 0)
                                 ? std::uint64_t(
                                       signed_element(a, left_width) >>
                                       (b & std::uint64_t(left_width - 1)))
                                 : a >> (b & std::uint64_t(left_width - 1)));
              if (tx_clip) {
                result_negative = bit(value, left_width - 1);
                low_negative = bit(value, tx_width - 1);
                overflow =
                    tx_clip_unsigned
                        ? (value >> tx_width) != 0
                        : (value >> tx_width) !=
                              (low_negative ? (UINT64_MAX >> (64 - tx_width))
                                            : 0);
                if (overflow) {
                  value = tx_clip_unsigned ? (UINT64_MAX >> (64 - tx_width))
                          : result_negative
                              ? std::uint64_t(1) << (tx_width - 1)
                              : (std::uint64_t(1) << (tx_width - 1)) - 1;
                  expected_saturated = 1;
                }
              }
            } else if (tx_widening) {
              value = bit(tx_opcode, 1) ? a - b : a + b;
            } else if (tx_saturating) {
              value = bit(tx_opcode, 1) ? a - b : a + b;
              wide_result = uint128(a) + uint128(b);
              overflow =
                  bit(tx_opcode, 0)
                      ? (bit(a, tx_width - 1) != bit(value, tx_width - 1)) &&
                            (bit(tx_opcode, 1)
                                 ? bit(a, tx_width - 1) != bit(b, tx_width - 1)
                                 : bit(a, tx_width - 1) == bit(b, tx_width - 1))
                  : bit(tx_opcode, 1) ? a < b
                                      : bit(wide_result, tx_width);
              if (overflow) {
                value = bit(tx_opcode, 0)
                            ? bit(a, tx_width - 1)
                                  ? std::uint64_t(1) << (tx_width - 1)
                                  : (std::uint64_t(1) << (tx_width - 1)) - 1
                        : bit(tx_opcode, 1) ? 0
                                            : lane_mask;
                expected_saturated = 1;
              }
            } else if (tx_average) {
              value =
                  rounded_average(a, b, tx_width, bit(tx_opcode, 0),
                                  bit(tx_opcode, 1), ((tx_vxrm)&low_mask(2)));
            } else
              switch (tx_opcode) {
              case 0:
                value = a + b;
                break;
              case 2:
                value = a - b;
                break;
              case 3:
                value = b - a;
                break;
              case 4:
                value = a < b ? a : b;
                break;
              case 5:
                value =
                    signed_element(a, tx_width) < signed_element(b, tx_width)
                        ? a
                        : b;
                break;
              case 6:
                value = a > b ? a : b;
                break;
              case 7:
                value =
                    signed_element(a, tx_width) > signed_element(b, tx_width)
                        ? a
                        : b;
                break;
              case 9:
                value = a & b;
                break;
              case 10:
                value = a | b;
                break;
              case 11:
                value = a ^ b;
                break;
              case 14:
                value = tx_mode == 6 && position == 0
                            ? broadcast_value
                            : element(tx_vs2, position - int(tx_distance),
                                      tx_width);
                break;
              case 15: {
                if (tx_mode == 6 && position == tx_vl - 1)
                  value = broadcast_value;
                else if (tx_distance >= std::uint64_t(tx_vlmax) ||
                         std::uint64_t(position) >=
                             std::uint64_t(tx_vlmax) - tx_distance)
                  value = 0;
                else
                  value =
                      element(tx_vs2, position + int(tx_distance), tx_width);
              } break;
              case 23:
                value = !tx_masked ||
                                bit(snapshot[position / XLEN], position % XLEN)
                            ? b
                            : a;
                break;
              case 24:
                value = std::uint64_t(a == b);
                break;
              case 25:
                value = std::uint64_t(a != b);
                break;
              case 26:
                value = std::uint64_t(a < b);
                break;
              case 27:
                value = std::uint64_t(signed_element(a, tx_width) <
                                      signed_element(b, tx_width));
                break;
              case 28:
                value = std::uint64_t(a <= b);
                break;
              case 29:
                value = std::uint64_t(signed_element(a, tx_width) <=
                                      signed_element(b, tx_width));
                break;
              case 30:
                value = std::uint64_t(a > b);
                break;
              case 31:
                value = std::uint64_t(signed_element(a, tx_width) >
                                      signed_element(b, tx_width));
                break;
              case 37:
                value = a << (b & std::uint64_t(tx_width - 1));
                break;
              case 39:
                value = a;
                break;
              case 40:
                value = a >> (b & std::uint64_t(tx_width - 1));
                break;
              case 41:
                value = signed_element(a, tx_width) >>
                        (b & std::uint64_t(tx_width - 1));
                break;
              case 42:
                value =
                    rounded_shift(a, tx_width, b, 0, ((tx_vxrm)&low_mask(2)));
                break;
              case 43:
                value =
                    rounded_shift(a, tx_width, b, 1, ((tx_vxrm)&low_mask(2)));
                break;
              default:
                fail(1, "bad reference opcode");
                break;
              }
            bit_offset = tx_compare ? position % XLEN
                         : tx_gather_vector || tx_narrowing
                             ? position * tx_width % XLEN
                             : lane * tx_width;
            expected_data |= (value & (tx_compare ? UINT64_C(1) : lane_mask))
                             << bit_offset;
            expected_mask |= (tx_compare ? UINT64_C(1) : lane_mask)
                             << bit_offset;
          }
        }
      CHECK(int(result.pfirst) == first && int(result.pend) == ending &&
            result.plast ==
                (ending == tx_vl && (!tx_compress || tx_compress_count == 0)));
      CHECK(std::uint64_t(result.pwrite.pmask) == expected_mask &&
            (std::uint64_t(result.pwrite.pdata) & expected_mask) ==
                expected_data);
      CHECK(result.psaturated == expected_saturated);
      if (expected_mask != 0) {
        CHECK(int(result.pwrite.paddress) == address);
        memory[address] =
            (memory[address] & ~word_t(expected_mask)) | word_t(expected_data);
      }
      if (tx_compress) {
        CHECK(result.pcompress_ustate.pdata ==
                  bits(tx_compress_buffer, XLEN - 1, 0) &&
              int(result.pcompress_ustate.pcount) == tx_compress_count &&
              int(result.pcompress_ustate.pdestination) ==
                  tx_compress_destination);
      }
      if (tx_dense && tx_beats != 0)
        // A final compression suffix becomes prepared at the source response,
        // then traverses S1/S2 with no reads. Body chunks remain consecutive.
        CHECK(last_commit_cycle +
                  ((tx_gather_vector || (tx_compress && first == tx_vl)) ? 2
                                                                         : 1) ==
              cycles);
      tx_beats++;
      if (last_commit_cycle + 1 == cycles)
        consecutive++;
      last_commit_cycle = cycles;
      tx_first = ending;
      checks++;
      if (result.plast)
        checking = 0;
    }
  }
  tick_model();
  if (was_retry)
    retry_enable = 0;
  cycles++;
}

void run_macro(int sew, int lmul, int count, int start, int op, int mode,
               int destination, int source1, int source2, bool masked_op,
               bool inject_retry = 0, bool random_stalls = 1, int retry_at = -1,
               int round_mode = 0) {
  int timeout;
  CHECK(!active && !checking);
  tx_widening = ((op >= 48 && op <= 55));
  tx_narrowing = ((op >= 44 && op <= 47));
  tx_saturating = ((op >= 32 && op <= 35));
  tx_average = ((op >= 8 && op <= 11)) && (mode == 2 || mode == 6);
  tx_rounding =
      ((op >= 42 && op <= 43) || (op >= 46 && op <= 47)) || tx_average;
  tx_clip = ((op >= 46 && op <= 47));
  tx_clip_unsigned = op == 46;
  tx_wide_source = ((op >= 52 && op <= 55)) || tx_narrowing;
  tx_widen_signed = tx_widening && bit(op, 0);
  tx_extension = op == 18 && mode == 2 && ((source1 >= 2 && source1 <= 7));
  tx_extension_signed = tx_extension && bit(source1, 0);
  tx_extension_ratio = tx_extension ? 8 >> ((source1 - 2) / 2) : 1;
  tx_carry_family = ((op >= 16 && op <= 19)) && !tx_extension;
  tx_carry_input = tx_carry_family && masked_op;
  tx_compress = op == 23 && mode == 2;
  tx_whole_move =
      op == 39 && mode == 3 &&
      (source1 == 0 || source1 == 1 || source1 == 3 || source1 == 7);
  tx_mask_logic = mode == 2 && !tx_extension && !tx_compress && !tx_widening &&
                  !tx_narrowing && !tx_average;
  tx_gather = op == 12 || (op == 14 && mode == 0);
  tx_gather_vector = tx_gather && mode == 0;
  tx_source_width = tx_mask_logic  ? 1
                    : tx_extension ? (8 << sew) / tx_extension_ratio
                                   : 8 << sew;
  tx_width = tx_widening    ? 2 * tx_source_width
             : tx_extension ? 8 << sew
                            : tx_source_width;
  tx_lanes =
      tx_gather_vector ? 1 : XLEN / (tx_narrowing ? 2 * tx_width : tx_width);
  tx_vl = tx_whole_move ? (source1 + 1) * VLEN / (8 << sew) : count;
  tx_start = start;
  tx_first = start / tx_lanes * tx_lanes;
  tx_opcode = op;
  tx_mode = mode;
  tx_vd = destination;
  tx_vs1 = source1;
  tx_vs2 = source2;
  tx_vxrm = round_mode;
  tx_masked = masked_op;
  tx_compare = (!tx_mask_logic && op >= 24 && op <= 31) ||
               (tx_carry_family && bit(op, 0));
  tx_dense = !random_stalls && !inject_retry;
  tx_beats = 0;
  tx_compress_buffer = 0;
  tx_compress_count = 0;
  tx_compress_destination = 0;
  tx_scalar = XLEN == 32 ? std::uint64_t(sign_extend(scalar, 32))
                         : std::uint64_t(scalar);
  tx_distance = mode == 6   ? 1
                : mode == 3 ? std::uint64_t(source1)
                            : std::uint64_t(scalar);
  tx_vlmax = tx_whole_move ? tx_vl
             : lmul < 4    ? (VLEN / (8 << sew)) << lmul
                           : (VLEN / (8 << sew)) >> (8 - lmul);
  for (int row = 0; row < DEPTH; row++)
    snapshot[row] = memory[row];
  instruction = (std::uint32_t(op) << 26) | (std::uint32_t(!masked_op) << 25) |
                (std::uint32_t(source2) << 20) |
                (std::uint32_t(source1) << 15) | (std::uint32_t(mode) << 12) |
                (std::uint32_t(destination) << 7) | UINT64_C(0x57);
  vxrm = ((round_mode)&low_mask(2));
  vtype = (word_t(sew) << 3) | word_t(lmul);
  vl = word_t(count);
  vstart = word_t(start);
  request_valid = 1;
  issue_ready = 1;
  retry_enable = inject_retry;
  retry_first = ((retry_at < 0 ? tx_lanes : retry_at) & low_mask(CW));
  checking = 1;
  eval();
  CHECK(request_ready && legal);
  tick();
  request_valid = 0;
  // Mutate live inputs immediately: all execution must use the captured descriptor.
  instruction = 0;
  vtype = 0;
  vl = 0;
  vstart = 0;
  scalar = ~scalar;
  vxrm = (~vxrm) & 3;
  timeout = 0;
  while (active || checking) {
    issue_ready = !random_stalls || (random_word() % 4 != 0);
    tick();
    if (timeout++ > 4000)
      fail(1,
           "sequencer failed to drain: op=%0d mode=%0d sew=%0d lmul=%0d vl=%0d "
           "start=%0d",
           op, mode, sew, lmul, count, start);
  }
  for (int repeat_index = 0; repeat_index < (5); ++repeat_index)
    tick();
  macros++;
}

int main() {
  return run_test([] {
    reset = 1;
    instruction = 0;
    vtype = 0;
    vl = 0;
    vstart = 0;
    scalar = word_t(-17);
    floating_scalar = 0;
    vxrm = 0;
    request_valid = 0;
    issue_ready = 0;
    cancel = 0;
    retry_enable = 0;
    retry_first = 0;
    initialize_in = {};
    checking = 0;
    last_commit_cycle = -100;
    for (int repeat_index = 0; repeat_index < (3); ++repeat_index)
      tick();
    reset = 0;
    for (int row = 0; row < DEPTH; row++) {
      initialize_in.pvalid = 1;
      initialize(((row)&low_mask(AW)), word_t(random_word()), UINT64_MAX);
      tick();
    }
    initialize_in.pvalid = 0;
    // Every SEW/LMUL geometry, including fractional groups, in-place operands,
    // vstart in the middle of a beat, tails, and signed scalar extension.
    for (int sew = 0; sew < SEW_LOG + 1; sew++) {
      for (int lm = 0; lm < 8; lm++) {
        int maximum;
        if (lm == 4 || (lm >= 5 && sew > lm - 8 + SEW_LOG))
          continue;
        maximum = VLEN / (8 << sew);
        maximum = lm < 4 ? maximum << lm : maximum >> (8 - lm);
        if (maximum == 0)
          continue;
        run_macro(sew, lm, maximum, 0, 0, 0, 24, 16, 8, 0,
                  maximum > XLEN / (8 << sew));
        run_macro(sew, lm, maximum - 1, maximum > 2 ? 1 : 0, 11, 4, 8, 3, 8, 1);
      }
      for (int op = 0; op < 42; op++) {
        if (!((op == 0 || (op >= 2 && op <= 7) || (op >= 9 && op <= 11) ||
               (op >= 24 && op <= 31) || op == 37 || op == 40 || op == 41)))
          continue;
        for (int mode_index = 0; mode_index < 3; mode_index++) {
          int mode;
          mode = mode_index == 0 ? 0 : mode_index == 1 ? 4 : 3;
          if (op == 3 && mode == 0)
            continue;
          if (((op == 30 || op == 31)) && mode == 0)
            continue;
          if (mode == 3 &&
              ((op == 2 || (op >= 4 && op <= 7) || op == 26 || op == 27)))
            continue;
          run_macro(sew, 0, VLEN / (8 << sew), 1, op, mode,
                    ((op >= 24 && op <= 31)) ? 0 : 24, mode == 0 ? 16 : 31, 8,
                    0);
        }
      }
      run_macro(sew, 0, 0, 0, 0, 0, 24, 16, 8, 0);
      run_macro(sew, 0, 1, 7, 0, 0, 24, 16, 8, 0);
    }
    // Extension reads a smaller-EEW/EMUL source while retaining destination
    // SEW/LMUL scheduling. Cover every legal ratio, geometry, and sign.
    for (int ratio_index = 0; ratio_index < 3; ratio_index++) {
      int power;
      power = ratio_index + 1;
      for (int sew = power; sew < SEW_LOG + 1; sew++) {
        for (int lm = 0; lm < 8; lm++) {
          int signed_lm, maximum, lanes;
          signed_lm = lm < 4 ? lm : lm - 8;
          if (lm == 4 || sew > signed_lm + SEW_LOG || signed_lm - power < -3)
            continue;
          maximum = lm < 4 ? ((VLEN / (8 << sew)) << lm)
                           : ((VLEN / (8 << sew)) >> (8 - lm));
          lanes = XLEN / (8 << sew);
          for (int signedness = 0; signedness < 2; signedness++)
            run_macro(sew, lm, maximum, maximum > 2 ? 1 : 0, 18, 2, 24,
                      6 - 2 * ratio_index + signedness, 8,
                      ((signedness)&low_mask(1)), maximum > lanes,
                      ((signedness)&low_mask(1)), lanes);
        }
      }
    }
    // Carry/borrow consumes v0 as operand data rather than predication. The
    // mask-producing forms optionally consume carry-in and may write v0.
    for (int sew = 0; sew < SEW_LOG + 1; sew++) {
      int maximum;
      maximum = VLEN / (8 << sew);
      for (int mode_index = 0; mode_index < 3; mode_index++) {
        int mode, source1;
        mode = mode_index == 0 ? 0 : mode_index == 1 ? 4 : 3;
        source1 = mode == 0 ? 16 : mode == 4 ? 3 : 31;
        scalar = word_t(-17);
        run_macro(sew, 0, maximum, 1, 16, mode, 24, source1, 8, 1);
        run_macro(sew, 0, maximum - 1, 0, 17, mode, 0, source1, 8, 1,
                  maximum > (XLEN / 8) >> sew, 1, (XLEN / 8) >> sew);
        run_macro(sew, 0, maximum, 0, 17, mode, 3, source1, 8, 0);
        if (mode != 3) {
          run_macro(sew, 0, maximum, 1, 18, mode, 24, source1, 8, 1);
          run_macro(sew, 0, maximum - 1, 0, 19, mode, 0, source1, 8, 1);
          run_macro(sew, 0, maximum, 0, 19, mode, 3, source1, 8, 0);
        }
      }
    }
    for (int sew = 0; sew < SEW_LOG + 1; sew++) {
      for (int lm = 0; lm < 8; lm++) {
        int maximum;
        if (lm == 4 || (lm >= 5 && sew > lm - 8 + SEW_LOG))
          continue;
        maximum = lm < 4 ? ((VLEN / (8 << sew)) << lm)
                         : ((VLEN / (8 << sew)) >> (8 - lm));
        run_macro(sew, lm, maximum, maximum > 2 ? 1 : 0, 16, 0, 24, 16, 8, 1, 0,
                  lm != 0);
        run_macro(sew, lm, maximum, 0, 19, 0, 3, 16, 8, 0, 0, lm != 0);
      }
    }
    // Narrow+narrow widening add/sub uses one destination-width beat per
    // source half. Exercise every legal SEW/LMUL, signedness, form, masks,
    // tails, vstart, upper halves, and the permitted high-source overlap.
    for (int sew = 0; sew < SEW_LOG; sew++) {
      for (int lm = 0; lm < 3; lm++) {
        int maximum, lanes, count;
        maximum = (VLEN / (8 << sew)) << lm;
        lanes = (XLEN / 16) >> sew;
        count = maximum < 2 * lanes + 1 ? maximum : 2 * lanes + 1;
        for (int op = 48; op < 52; op++) {
          run_macro(sew, lm, count, bit(op, 0) ? 1 : 0, op, 2, 24, 16, 8,
                    bit(op, 0), op == 49, op != 50, lanes);
          scalar = word_t(-17);
          run_macro(sew, lm, count - 1, count > 2 ? lanes - 1 : 0, op, 6, 24, 3,
                    8, bit(op, 0));
        }
      }
      run_macro(sew, 0, VLEN / (8 << sew), 0, 51, 2, 8, 16, 9, 0, 1, 0,
                (XLEN / 16) >> sew);
    }
    // Wide+narrow widening reuses the destination-width schedule. vs2 reads
    // one wide row per beat while vector/scalar vs1 still selects a narrow half.
    for (int sew = 0; sew < SEW_LOG; sew++) {
      for (int lm = 0; lm < 3; lm++) {
        int maximum, lanes, count;
        maximum = (VLEN / (8 << sew)) << lm;
        lanes = (XLEN / 16) >> sew;
        count = maximum < 2 * lanes + 1 ? maximum : 2 * lanes + 1;
        for (int op = 52; op < 56; op++) {
          run_macro(sew, lm, count, bit(op, 0) ? 1 : 0, op, 2, 24, 16, 8,
                    bit(op, 0), op == 53, op != 54, lanes);
          scalar = word_t(-17);
          run_macro(sew, lm, count - 1, count > 2 ? lanes - 1 : 0, op, 6, 24, 3,
                    8, bit(op, 0));
        }
      }
      // Equal-width destination/vs2 overlap and high-part narrow-vs1 overlap.
      run_macro(sew, 0, VLEN / (8 << sew), 0, 55, 2, 8, 9, 8, 0, 1, 0,
                (XLEN / 16) >> sew);
    }
    // Narrowing shifts consume one doubled-width source row and write one
    // destination half-row per beat through the existing SIMD shifter.
    for (int sew = 0; sew < SEW_LOG; sew++) {
      for (int lm_index = 0; lm_index < 6; lm_index++) {
        int lm, exponent, maximum, lanes, count;
        lm = lm_index < 3 ? lm_index : lm_index + 2;
        exponent = lm < 4 ? lm : lm - 8;
        if (sew > exponent + SEW_LOG)
          continue;
        maximum = exponent >= 0 ? (VLEN / (8 << sew)) << exponent
                                : (VLEN / (8 << sew)) >> -exponent;
        if (maximum == 0)
          continue;
        lanes = (XLEN / 16) >> sew;
        count = maximum < 2 * lanes + 1 ? maximum : 2 * lanes + 1;
        for (int op = 44; op < 46; op++) {
          run_macro(sew, lm, count, bit(op, 0) ? 1 : 0, op, 0, 24, 16, 8,
                    bit(op, 0), op == 44, 0, lanes);
          scalar = word_t(-17);
          run_macro(sew, lm, count - 1, count > 2 ? lanes - 1 : 0, op, 4, 24, 3,
                    8, !bit(op, 0));
          run_macro(sew, lm, count, 0, op, 3, 24, 31, 8, bit(op, 0));
        }
      }
      // Low-part in-place overlap remains safe across partial destination rows.
      run_macro(sew, 0, VLEN / (8 << sew), 0, 45, 0, 8, 16, 8, 0, 1, 0,
                (XLEN / 16) >> sew);
    }
    // Saturating add/sub uses the packed adder's lane carry/sign results;
    // averaging retains the infinite-precision extension through vxrm rounding.
    for (int sew = 0; sew < SEW_LOG + 1; sew++) {
      int maximum;
      maximum = VLEN / (8 << sew);
      for (int op = 32; op < 36; op++) {
        run_macro(sew, 0, maximum, int(bit(op, 0)), op, 0, 24, 16, 8,
                  bit(op, 0), op == 32, 0, XLEN / (8 << sew));
        scalar = word_t(-17);
        run_macro(sew, 0, maximum - 1, maximum > 2 ? 1 : 0, op, 4, 24, 3, 8,
                  !bit(op, 0));
        if (!bit(op, 1))
          run_macro(sew, 0, maximum, 0, op, 3, 24, 31, 8, 0);
      }
      for (int round_mode = 0; round_mode < 4; round_mode++) {
        for (int op = 8; op < 12; op++) {
          run_macro(sew, 0, maximum, int(bit(round_mode, 0)), op, 2, 24, 16, 8,
                    bit(round_mode, 1), op == 8, 0, XLEN / (8 << sew),
                    round_mode);
          scalar = word_t(sew * 11) - word_t(round_mode) - word_t(9);
          run_macro(sew, 0, maximum - 1, maximum > 2 ? 1 : 0, op, 6, 24, 3, 8,
                    !bit(round_mode, 0), 0, 1, -1, round_mode);
        }
      }
    }
    // Scaling shifts use vxrm on equal-width elements. Narrowing clips round
    // the doubled-width source first, then saturate each active lane and report
    // a per-beat sticky-CSR contribution. Live vxrm changes after admission
    // must not affect the captured macro.
    for (int sew = 0; sew < SEW_LOG + 1; sew++) {
      int maximum;
      maximum = VLEN / (8 << sew);
      for (int round_mode = 0; round_mode < 4; round_mode++) {
        for (int op = 42; op < 44; op++) {
          run_macro(sew, 0, maximum, 0, op, 0, 24, 16, 8, bit(round_mode, 0), 0,
                    1, -1, round_mode);
          scalar = word_t(sew * 7 + round_mode);
          run_macro(sew, 0, maximum - 1, maximum > 2 ? 1 : 0, op, 4, 24, 3, 8,
                    bit(round_mode, 0), 0, 1, -1, round_mode);
          run_macro(sew, 0, maximum, 0, op, 3, 24, 31, 8, 0, 0, 1, -1,
                    round_mode);
        }
      }
    }
    for (int sew = 0; sew < SEW_LOG; sew++) {
      int maximum, lanes;
      maximum = VLEN / (8 << sew);
      lanes = (XLEN / 16) >> sew;
      for (int round_mode = 0; round_mode < 4; round_mode++) {
        for (int op = 46; op < 48; op++) {
          run_macro(sew, 0, maximum, 0, op, 0, 24, 16, 8, bit(round_mode, 0),
                    round_mode == 1, 1, lanes, round_mode);
          scalar = word_t(sew * 5 + round_mode);
          run_macro(sew, 0, maximum - 1, maximum > 2 ? 1 : 0, op, 4, 24, 3, 8,
                    !bit(round_mode, 0), 0, 1, -1, round_mode);
          run_macro(sew, 0, maximum, 0, op, 3, 24, 31, 8, 0, 0, 1, -1,
                    round_mode);
        }
      }
    }
    // Moves and merge share an encoding but not predication: a zero v0 bit
    // selects vs2; it must not disable the destination write.
    for (int sew = 0; sew < SEW_LOG + 1; sew++) {
      for (int lm = 0; lm < 8; lm++) {
        int maximum;
        if (lm == 4 || (lm >= 5 && sew > lm - 8 + SEW_LOG))
          continue;
        maximum = lm < 4 ? ((VLEN / (8 << sew)) << lm)
                         : ((VLEN / (8 << sew)) >> (8 - lm));
        for (int form = 0; form < 3; form++) {
          int mode, src;
          mode = form == 0 ? 0 : form == 1 ? 4 : 3;
          src = form == 0 ? 16 : 31;
          run_macro(sew, lm, maximum, 0, 23, mode, 24, src, 0, 0);
          run_macro(sew, lm, maximum - 1, maximum > 2 ? 1 : 0, 23, mode, 8, src,
                    8, 1);
          // Legal moves to v0 and in-place vector-source moves.
          run_macro(sew, lm, maximum, 0, 23, mode, form == 0 ? 16 : 0, src, 0,
                    0);
        }
      }
      // All mask instructions address single registers, even with LMUL=8.
      // Vary SEW while keeping valid VL and sweep in-place operands/destination v0.
      for (int op = 24; op < 32; op++) {
        int maximum;
        maximum = VLEN >> sew;
        run_macro(sew, 3, maximum, 0, op, 2, 3, 5, 7, 0, 0, 0);
        run_macro(sew, 3, maximum - 1, 3, op, 2, 5, 5, 7, 0);
        run_macro(sew, 3, maximum, maximum > XLEN ? XLEN - 1 : 1, op, 2, 0, 5,
                  0, 0, 1, 1, 0);
      }
      run_macro(sew, 0, 0, 7, 23, 3, 0, 31, 0, 0);
      run_macro(sew, 0, 1, 7, 23, 4, 8, 3, 8, 1);
      run_macro(sew, 0, 0, 7, 31, 2, 0, 5, 7, 0);
    }
    // Whole-register moves ignore vl and LMUL, copy NREG complete registers,
    // honor SEW-granular vstart, and naturally update the dedicated v0 shadow.
    for (int sew = 0; sew < SEW_LOG + 1; sew++) {
      for (int registers = 1; registers <= 8; registers *= 2) {
        int effective, lanes;
        effective = registers * VLEN / (8 << sew);
        lanes = (XLEN / 8) >> sew;
        run_macro(sew, 0, 0, 0, 39, 3, 16, registers - 1, 8, 0, 1, 0, lanes);
        run_macro(sew, 0, 1, 1, 39, 3, 0, registers - 1, 8, 0, registers == 8,
                  1, lanes);
        run_macro(sew, 3, VLEN, 0, 39, 3, 8, registers - 1, 8, 0);
        CHECK(effective > lanes);
      }
    }
    // Slides read across chunks/groups while rotating through the same E64
    // SIMD slot. Golden values come from the original architectural snapshot.
    for (int sew = 0; sew < SEW_LOG + 1; sew++) {
      for (int lm = 0; lm < 8; lm++) {
        int maximum, lanes;
        if (lm == 4 || (lm >= 5 && sew > lm - 8 + SEW_LOG))
          continue;
        maximum = lm < 4 ? (VLEN / (8 << sew)) << lm
                         : (VLEN / (8 << sew)) >> (8 - lm);
        lanes = (XLEN / 8) >> sew;
        for (int form = 0; form < 6; form++) {
          int op, mode, dest;
          op = (form == 0 || form == 1 || form == 4) ? 14 : 15;
          mode = form >= 4 ? 6 : (form == 1 || form == 3) ? 3 : 4;
          for (int scenario = 0; scenario < 12; scenario++) {
            int amount, length, start;
            bool masked;
            amount = scenario < 8     ? scenario
                     : scenario == 8  ? 31
                     : scenario == 9  ? maximum - 1
                     : scenario == 10 ? maximum
                                      : maximum + 1;
            scalar = mode == 6 ? word_t(-17) : word_t(amount);
            dest = op == 15 && bit(scenario, 0) ? 8 : 24;
            length = scenario == 2   ? 0
                     : scenario == 3 ? 1
                     : scenario == 4 ? maximum - 1
                                     : maximum;
            start = scenario == 5   ? 1
                    : scenario == 6 ? lanes - 1
                    : scenario == 7 ? length
                                    : 0;
            masked = bit(scenario, 0);
            run_macro(sew, lm, length, start, op, mode, dest,
                      mode == 3 ? amount & 31 : 3, 8, masked, 0, scenario != 0);
          }
          // Bit 8 and the XLEN sign bit must not truncate to SEW or an address.
          if (mode == 4) {
            scalar = word_t(256);
            run_macro(sew, lm, maximum, 0, op, mode, 24, 3, 8, 0);
            scalar = word_t(1) << (XLEN - 1);
            run_macro(sew, lm, maximum, 0, op, mode, 24, 3, 8, 0);
            scalar = UINT64_MAX;
            run_macro(sew, lm, maximum, 0, op, mode, 24, 3, 8, 0);
          }
          // Read-before-write ordering preserves an in-place downward suffix
          // when the first partial chunk or a later chunk must be reread.
          if (maximum > lanes) {
            scalar = mode == 6 ? word_t(-37) : 1;
            run_macro(sew, lm, maximum, lanes > 1 ? 1 : 0, op, mode,
                      op == 15 ? 8 : 24, 1, 8, 1, 1, 1, 0);
            scalar = mode == 6 ? word_t(-37) : 1;
            run_macro(sew, lm, maximum, lanes > 1 ? 1 : 0, op, mode,
                      op == 15 ? 8 : 24, 1, 8, 1, 1, 1, lanes);
          }
          scalar = mode == 6 ? word_t(-17) : 3;
          run_macro(sew, lm, maximum, 0, op, mode, op == 15 ? 8 : 24, 3, 8, 0,
                    0, 0);
        }
      }
    }
    // Gather reads arbitrary source positions, with a separate EEW16 index
    // stream. Source values are modeled from the admission-time snapshot.
    for (int sew = 0; sew < SEW_LOG + 1; sew++) {
      for (int lm = 0; lm < 8; lm++) {
        int maximum, exponent;
        exponent = lm < 4 ? lm : lm - 8;
        if (lm == 4 || sew > exponent + SEW_LOG)
          continue;
        maximum = exponent >= 0 ? (VLEN / (8 << sew)) << exponent
                                : (VLEN / (8 << sew)) >> (-exponent);
        for (int form = 0; form < 4; form++) {
          int iw, groups, ig, op, mode;
          iw = form == 1 ? 16 : 8 << sew;
          ig = exponent + (form == 1 ? 1 - sew : 0);
          if (form == 1 && (ig < -3 || ig > 3))
            continue;
          groups = ig > 0 ? 1 << ig : 1;
          op = form == 1 ? 14 : 12;
          mode = form < 2 ? 0 : form == 2 ? 4 : 3;
          // Refresh data/destination after the preceding destructive slide
          // sweeps, so indexed selection cannot pass on an all-zero source.
          for (int r = 8; r < 32; r++) {
            if (r >= 16 && r < 24)
              continue;
            for (int row = 0; row < VLEN / XLEN; row++) {
              initialize(((r * VLEN / XLEN + row) & low_mask(AW)),
                         word_t(random_word()), UINT64_MAX);
              initialize_in.pvalid = 1;
              tick();
            }
          }
          initialize_in.pvalid = 0;
          if (form < 2) {
            for (int row = 0; row < groups * VLEN / XLEN; row++) {
              std::uint64_t data, idx;
              data = 0;
              for (int lane = 0; lane < XLEN / iw; lane++) {
                int i;
                i = row * (XLEN / iw) + lane;
                switch (i % 8) {
                case 0:
                  idx = 0;
                  break;
                case 1:
                  idx = std::uint64_t(maximum - 1);
                  break;
                case 2:
                  idx = std::uint64_t(maximum);
                  break;
                case 3:
                  idx = UINT64_MAX;
                  break;
                case 4:
                  idx = std::uint64_t(256);
                  break;
                default:
                  idx = random_word() % std::uint64_t(maximum);
                  break;
                }
                data |= (idx & (UINT64_MAX >> (64 - iw))) << (lane * iw);
              }
              initialize(((16 * VLEN / XLEN + row) & low_mask(AW)),
                         word_t(data), UINT64_MAX);
              initialize_in.pvalid = 1;
              tick();
            }
            initialize_in.pvalid = 0;
          }
          for (int scenario = 0; scenario < 9; scenario++) {
            int length, start, lanes;
            lanes = form < 2 ? 1 : (XLEN / 8) >> sew;
            length = scenario == 0 ? 0 : scenario == 1 ? maximum - 1 : maximum;
            start = scenario == 2 ? 1 : scenario == 3 ? maximum : 0;
            scalar = scenario == 4   ? word_t(maximum - 1)
                     : scenario == 5 ? word_t(maximum)
                     : scenario == 6 ? word_t(256)
                     : scenario == 7 ? word_t(1) << (XLEN - 1)
                     : scenario == 8 ? UINT64_MAX
                                     : word_t(3);
            run_macro(sew, lm, length, start, op, mode, 24,
                      form < 2    ? 16
                      : form == 3 ? 31
                                  : 3,
                      8, bit(scenario, 0), 0, scenario != 4);
            if (maximum > lanes && scenario == 4) {
              scalar = word_t(maximum - 1);
              run_macro(sew, lm, maximum, 0, op, mode, 24, form < 2 ? 16 : 3, 8,
                        1, 1, 1, lanes);
              scalar = word_t(maximum - 1);
              run_macro(sew, lm, maximum, 0, op, mode, 24, form < 2 ? 16 : 3, 8,
                        0, 1, 1, 0);
            }
          }
          // Equal-EEW source aliases are legal; the destination stays disjoint.
          if (form == 0 || (form == 1 && sew == 1))
            run_macro(sew, lm, maximum, 0, op, 0, 24, 8, 8, 0);
        }
      }
    }
    // Compress streams source chunks in order, checkpoints its packed suffix
    // at WB, and writes consecutive destination chunks without extra VRF ports.
    for (int sew = 0; sew < SEW_LOG + 1; sew++) {
      for (int lm = 0; lm < 8; lm++) {
        int exponent, maximum, lanes;
        exponent = lm < 4 ? lm : lm - 8;
        if (lm == 4 || sew > exponent + SEW_LOG)
          continue;
        maximum = exponent >= 0 ? (VLEN / (8 << sew)) << exponent
                                : (VLEN / (8 << sew)) >> (-exponent);
        lanes = (XLEN / 8) >> sew;
        for (int pattern = 0; pattern < 4; pattern++) {
          for (int row = 0; row < VLEN / XLEN; row++) {
            std::uint64_t mask_data;
            mask_data = pattern == 0   ? 0
                        : pattern == 1 ? UINT64_MAX
                        : pattern == 2 ? UINT64_C(0xd4924924a529294a)
                                       : random_word();
            initialize(((5 * VLEN / XLEN + row) & low_mask(AW)),
                       word_t(mask_data), UINT64_MAX);
            initialize_in.pvalid = 1;
            tick();
          }
          initialize_in.pvalid = 0;
          run_macro(sew, lm,
                    pattern == 0   ? maximum
                    : pattern == 1 ? maximum - 1
                                   : maximum,
                    0, 23, 2, 24, 5, 8, 0, pattern == 3 && maximum > lanes,
                    pattern != 2, pattern == 3 ? lanes : -1);
        }
        run_macro(sew, lm, 0, 0, 23, 2, 24, 5, 8, 0);
      }
    }
    // Exercise in-place prefixes and first partial rows around stateful cases.
    for (int ones = 0; ones < 2; ones++) {
      for (int row = 0; row < VLEN / XLEN; row++) {
        initialize(((row)&low_mask(AW)), ones != 0 ? UINT64_MAX : 0,
                   UINT64_MAX);
        initialize_in.pvalid = 1;
        tick();
      }
      initialize_in.pvalid = 0;
      run_macro(0, 3, VLEN, 0, 23, 0, 8, 16, 8, 1, 0, 0);
      scalar = 1;
      run_macro(0, 3, VLEN, 0, 14, 4, 24, 3, 8, 1, 0, 0);
      scalar = 1;
      run_macro(0, 3, VLEN, 0, 15, 4, 8, 3, 8, 1, 0, 0);
      scalar = word_t(-17);
      run_macro(0, 3, VLEN, 0, 14, 6, 24, 3, 8, 1, 0, 0);
      scalar = word_t(-17);
      run_macro(0, 3, VLEN, 0, 15, 6, 8, 3, 8, 1, 0, 0);
      scalar = 3;
      run_macro(0, 3, VLEN, 0, 12, 4, 24, 3, 8, 1, 0, 0);
      run_macro(0, 3, VLEN, 0, 12, 0, 24, 16, 8, 1);
    }
    run_macro(0, 3, VLEN - 1, 3, 27, 2, 3, 5, 3, 0, 1, 1, XLEN);
    run_macro(0, 3, VLEN - 1, 3, 23, 0, 8, 16, 8, 1, 1, 1, 8);
    run_macro(0, 3, VLEN, 0, 0, 0, 24, 16, 8, 0, 0, 0);
    // Initial partial chunks and later in-place chunks must not rewrite
    // pre-vstart elements.
    run_macro(0, 0, VLEN / 8, 3, 0, 4, 8, 3, 8, 0, 1, 1, 0);
    run_macro(0, 0, VLEN / 8, 3, 0, 4, 8, 3, 8, 0, 1, 1, 8);
    initialize(((0) & low_mask(AW)), word_t(UINT64_C(0xaaaaaaaaaaaaaaa5)),
               UINT64_MAX);
    initialize_in.pvalid = 1;
    tick();
    initialize_in.pvalid = 0;
    run_macro(0, 0, VLEN / 8, 0, 25, 0, 0, 16, 8, 1);
    CHECK(consecutive >= VLEN / 8 - 1 && retries > 0);
    // Cancel at read, buffered-offer, and pre-WB boundaries. No killed token
    // may update the bank or be mistaken for the next macro's response.
    for (int family = 0; family < 11; family++) {
      for (int delay = 0; delay < 4; delay++) {
        instruction = family == 0   ? UINT64_C(0x02880c57)
                      : family == 1 ? UINT64_C(0x5c880c57)
                      : family == 2 ? UINT64_C(0x5e080c57)
                      : family == 3 ? UINT64_C(0x6e72a1d7)
                      : family == 4 ? UINT64_C(0x3a81cc57)
                      : family == 5 ? UINT64_C(0x3e81e457)
                      : family == 6 ? UINT64_C(0x32880c57)
                      : family == 7 ? UINT64_C(0x3a880c57)
                      : family == 8 ? UINT64_C(0x5e82ac57)
                      : family == 9 ? UINT64_C(0xce816457)
                                    : UINT64_C(0x9e83b857);
        vtype = 0;
        vl = word_t(VLEN / 8);
        vstart = 0;
        request_valid = 1;
        issue_ready = delay == 3;
        tick();
        request_valid = 0;
        for (int repeat_index = 0; repeat_index < (delay); ++repeat_index)
          tick();
        cancel = 1;
        tick();
        cancel = 0;
        for (int repeat_index = 0; repeat_index < (7); ++repeat_index)
          tick();
      }
    }
    for (int delay = 1; delay <= 3; delay++) {
      instruction = UINT64_C(0x32880c57);
      vtype = 0;
      vl = word_t(VLEN / 8);
      vstart = 0;
      request_valid = 1;
      issue_ready = 0;
      tick();
      request_valid = 0;
      for (int repeat_index = 0; repeat_index < (delay); ++repeat_index)
        tick();
      reset = 1;
      tick();
      reset = 0;
      for (int repeat_index = 0; repeat_index < (7); ++repeat_index)
        tick();
    }
    run_macro(0, 0, VLEN / 8, 0, 11, 0, 8, 8, 8, 0);
    std::cout << "macros=" << macros << " beats=" << checks
              << " retries=" << retries << " cycles=" << cycles << "\n";
  });
}
