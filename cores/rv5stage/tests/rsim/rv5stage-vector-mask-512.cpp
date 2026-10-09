// Preserves the rv5stage-vector-mask-512 cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
// Checks mask scans and reductions across eight VRF words, including SEW8 count/index wrap.

// Models reductions, scans, slides, gathers, and compression through public LSU readback and cancellation.

std::uint64_t model[32][8];
std::uint64_t rng = UINT64_C(0x651b3c5defab7809), scalar_expected;
int mode = 0, regno = 0, cycles = 0, checks = 0, macros = 0;
int retired_count, resolved_count, scalar_count, saturate_count;
bool dense_reduction = 0;
int dense_last_issue, dense_issues;
std::uint64_t random_word() {
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return rng;
}
std::uint64_t element(int r, int i, int width) {
  return (model[r + i * width / 512][(i * width % 512) / 64] >>
          (i * width % 64)) &
         (UINT64_MAX >> (64 - width));
}
std::uint64_t sext(std::uint64_t x, int width) {
  return ((std::int64_t(sign_extend(x << (64 - width), 64)) >> (64 - width)) &
          low_mask(64));
}
std::uint32_t vec(int code, int d, int s2, int s1, int f3,
                  std::uint8_t masked = 0) {
  return (((code << 26) | (int(!masked) << 25) | (s2 << 20) | (s1 << 15) |
           (f3 << 12) | (d << 7) | UINT64_C(87)) &
          low_mask(32));
}
std::uint64_t fold(int op, int width, std::uint64_t a, std::uint64_t b) {
  switch (op) {
  case 0: {
    return (a + b) & (UINT64_MAX >> (64 - width));
  } break;
  case 1: {
    return a & b;
  } break;
  case 2: {
    return a | b;
  } break;
  case 3: {
    return a ^ b;
  } break;
  case 4: {
    return a < b ? a : b;
  } break;
  case 5: {
    return std::int64_t(sign_extend(sext(a, width), 64)) <
                   std::int64_t(sign_extend(sext(b, width), 64))
               ? a
               : b;
  } break;
  case 6: {
    return a > b ? a : b;
  } break;
  default: {
    return std::int64_t(sign_extend(sext(a, width), 64)) >
                   std::int64_t(sign_extend(sext(b, width), 64))
               ? a
               : b;
  } break;
  }
}

void tick() {
  settle();
  if (!reset) {
    if (dense_reduction && issued) {
      if (dense_issues != 0)
        CHECK(cycles == dense_last_issue + 1);
      dense_last_issue = cycles;
      dense_issues++;
    }
    if (memory_committed) {
      if (mode == 1)
        CHECK(regno + int(wb_index) / 8 < 32);
      resolved_count++;
      if (mode == 2) {
        CHECK(((store_data)&low_mask(64)) ==
              ((element(regno, int(wb_index), 64)) & low_mask(64)));
        checks++;
      }
    }
    if (compute_matured)
      resolved_count++;
    if (saturate) {
      CHECK(compute_matured);
      saturate_count++;
    }
    if (scalar_result_out.pvalid) {
      CHECK(mode == 3 && scalar_result_out.pbits.paddress == 5 &&
            scalar_result_out.pbits.pdata == ((scalar_expected)&low_mask(64)));
      scalar_count++;
      checks++;
    }
    if (retired)
      retired_count++;
  }
  rising();
  settle();
  falling();
  cycles++;
  if (cycles > 1500000)
    fail(1, "pipeline timeout");
}
void run(std::uint32_t insn, int sew, int lm, int length, int start = 0,
         int kill_after = -1, std::uint8_t dense_issue = 0) {
  int timeout;
  instruction = insn;
  vtype = (((sew)&low_mask(64)) << 3) | ((lm)&low_mask(64));
  vl = ((length)&low_mask(64));
  vstart = ((start)&low_mask(64));
  retired_count = 0;
  resolved_count = 0;
  scalar_count = 0;
  timeout = 0;
  dense_reduction = dense_issue;
  dense_issues = 0;
  dense_last_issue = -1;
  if (dense_issue)
    issue_ready = 1;
  request_valid = 1;
  while (!request_ready)
    tick();
  tick();
  request_valid = 0;
  while (active) {
    issue_ready = dense_issue || ((random_word() & 3) != 0);
    tick();
    if (kill_after >= 0 && resolved_count >= kill_after && active) {
      cancel = 1;
      tick();
      cancel = 0;
      break;
    }
    timeout++;
    if (timeout > 20000)
      fail(1, "macro stuck insn=%h wb=%0d", insn, wb_index);
  }
  for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
    tick();
  if (dense_issue)
    CHECK(dense_issues == length);
  dense_reduction = 0;
  CHECK(retired_count == (kill_after < 0 ? 1 : 0));
  if (mode == 3)
    CHECK(scalar_count == (kill_after < 0 ? 1 : 0));
  issue_ready = 1;
  macros++;
}
// Public LSU completions initialize storage; this test adapter is not an RV32 memory-ISA claim.
void load_reg(int r) {
  mode = 1;
  regno = r;
  run(((UINT64_C(0x2000007) | ((64 == 64 ? 7 : 6) << 12) | (r << 7)) &
       low_mask(32)),
      64 == 64 ? 3 : 2, 0, 512 / 64);
  mode = 0;
}
void check_reg(int r) {
  mode = 2;
  regno = r;
  run(((UINT64_C(0x2000027) | ((64 == 64 ? 7 : 6) << 12) | (r << 7)) &
       low_mask(32)),
      64 == 64 ? 3 : 2, 0, 512 / 64);
  mode = 0;
}
void widening_reduction_case(std::uint8_t signed_operation, int sew, int lm,
                             int length, int dest, int seed = 3, int source = 8,
                             std::uint8_t masked = 0, int kill_after = -1) {
  std::uint64_t acc, value, wide_mask;
  int width, wide_width;
  width = 8 << sew;
  wide_width = 2 * width;
  wide_mask = UINT64_MAX >> (64 - wide_width);
  acc = element(seed, 0, wide_width);
  for (int i = 0; i < length; i++)
    if (!masked || element(0, i, 1) != 0) {
      value = element(source, i, width);
      if (signed_operation)
        value = sext(value, width);
      acc = (acc + value) & wide_mask;
    }
  run(vec(signed_operation ? 49 : 48, dest, source, seed, 0, masked), sew, lm,
      length, 0, kill_after);
  if (kill_after < 0 && length != 0)
    model[dest][0] = (model[dest][0] & ~wide_mask) | (acc & wide_mask);
  check_reg(dest);
}
void scan_case(int op, int sew, int lm, int length, int pattern,
               std::uint8_t masked, int kill_after = -1, int start = 0) {
  std::uint64_t expected[64];
  std::uint64_t value, lane_mask;
  int count, first_set, width, lanes, dest, source, selector, row, offset,
      written, groups;
  bool selected, active_element;
  width = 8 << sew;
  lanes = op < 5 ? 64 : 64 / width;
  dest = op < 2 ? 5 : op < 5 ? 3 : 16;
  source = pattern == 6 ? 0 : 7;
  groups = op < 5 ? 1 : lm < 4 ? 1 << lm : 1;
  for (int c = 0; c < 8; c++) {
    model[0][c] = pattern == 5 ? 0 : masked ? random_word() : UINT64_MAX;
    model[7][c] = pattern == 0   ? 0
                  : pattern == 1 ? UINT64_MAX
                  : pattern == 2 || pattern == 3 || pattern == 4
                      ? 0
                      : random_word();
  }
  if (pattern >= 2 && pattern <= 4) {
    int position;
    position = pattern == 2 ? 63 : pattern == 3 ? 64 : 512 - 1;
    model[7][position / 64] = ((1) & low_mask(64)) << (position % 64);
  }
  load_reg(0);
  load_reg(7);
  for (int c = 0; c < groups * 8; c++)
    expected[c] = model[dest + c / 8][c % 8];
  count = 0;
  first_set = -1;
  // Prefix/index golden model uses architectural elements, never RTL chunks.
  for (int i = 0; i < length; i++) {
    active_element = !masked || element(0, i, 1) != 0;
    selected = active_element && element(source, i, 1) != 0;
    if (op >= 2 && i >= start && active_element) {
      switch (op) {
      case 2: {
        value = ((first_set < 0 && !selected) & low_mask(64));
      } break;
      case 3: {
        value = ((first_set < 0) & low_mask(64));
      } break;
      case 4: {
        value = ((first_set < 0 && selected) & low_mask(64));
      } break;
      case 5: {
        value = ((count)&low_mask(64));
      } break;
      default: {
        value = ((i)&low_mask(64));
      } break;
      }
      row = op < 5 ? i / 64 : i * width / 64;
      offset = op < 5 ? i % 64 : i * width % 64;
      lane_mask = op < 5 ? 1 : UINT64_MAX >> (64 - width);
      expected[row] = (expected[row] & ~(lane_mask << offset)) |
                      ((value & lane_mask) << offset);
    }
    if (selected) {
      count++;
      if (first_set < 0)
        first_set = i;
    }
  }
  selector = op == 0   ? 16
             : op == 1 ? 17
             : op == 2 ? 1
             : op == 3 ? 3
             : op == 4 ? 2
             : op == 5 ? 16
                       : 17;
  if (op < 2) {
    scalar_expected =
        op == 0 ? ((count)&low_mask(64))
                : ((std::int64_t(sign_extend(first_set, 64))) & low_mask(64));
    mode = 3;
  }
  run(vec(op < 2 ? 16 : 20, dest, op == 6 ? 0 : source, selector, 2, masked),
      sew, lm, length, start, kill_after);
  mode = 0;
  if (op >= 2) {
    written = kill_after < 0 ? length : resolved_count * lanes;
    for (int i = start; i < length && i < written; i++) {
      row = op < 5 ? i / 64 : i * width / 64;
      offset = op < 5 ? i % 64 : i * width % 64;
      lane_mask = (op < 5 ? ((1) & low_mask(64)) : UINT64_MAX >> (64 - width))
                  << offset;
      model[dest + row / 8][row % 8] =
          (model[dest + row / 8][row % 8] & ~lane_mask) |
          (expected[row] & lane_mask);
    }
    for (int r = 0; r < groups; r++)
      check_reg(dest + r);
  }
}
void slide_case(int form, int sew, int length, int start, std::uint8_t masked,
                int kill_after = -1, std::uint8_t inplace = 0) {
  std::uint64_t expected[64];
  std::uint64_t value, lane_mask;
  int width, lanes, dest, source, group_elements, count, written, row, offset;
  bool up, one;
  kill_after = -1;
  up = (form == 0 || form == 1 || form == 4);
  one = form >= 4;
  dest = inplace ? 8 : 16;
  source = 8;
  width = 8 << sew;
  lanes = 64 / width;
  group_elements = 512 * 8 / width;
  count = one ? 1 : 3;
  scalar = one ? ((-19) & low_mask(64)) : ((count)&low_mask(64));
  for (int c = 0; c < 8 * 8; c++)
    expected[c] = model[dest + c / 8][c % 8];
  for (int i = start; i < length; i++) {
    if (masked && element(0, i, 1) == 0)
      continue;
    if (up && !one && i < count)
      continue;
    if (one && i == (up ? 0 : length - 1))
      value = sext(((scalar)&low_mask(64)), 64);
    else if (!up && i + count >= group_elements)
      value = 0;
    else
      value = element(source, up ? i - count : i + count, width);
    row = i * width / 64;
    offset = i * width % 64;
    lane_mask = (UINT64_MAX >> (64 - width)) << offset;
    expected[row] =
        (expected[row] & ~lane_mask) | ((value << offset) & lane_mask);
  }
  run(vec(up ? 14 : 15, dest, source, (form == 1 || form == 3) ? count : 3,
          one                        ? 6
          : (form == 1 || form == 3) ? 3
                                     : 4,
          masked),
      sew, 3, length, start, kill_after);
  written = kill_after < 0 ? length : (start / lanes + resolved_count) * lanes;
  for (int i = start; i < length && i < written; i++) {
    row = i * width / 64;
    offset = i * width % 64;
    lane_mask = (UINT64_MAX >> (64 - width)) << offset;
    model[dest + row / 8][row % 8] =
        (model[dest + row / 8][row % 8] & ~lane_mask) |
        (expected[row] & lane_mask);
  }
  for (int r = 0; r < 8; r++)
    check_reg(dest + r);
}
void gather_case(int form, int sew, int lm, int length, int start,
                 std::uint8_t masked, int kill_after = -1) {
  std::uint64_t expected[64];
  std::uint64_t value, lane_mask, idx;
  std::uint32_t insn;
  int width, iw, groups, igroups, exponent, ie, maximum, lanes, row, offset,
      written;
  kill_after = -1;
  width = 8 << sew;
  iw = form == 1 ? 16 : width;
  exponent = lm < 4 ? lm : lm - 8;
  ie = exponent + (form == 1 ? 1 - sew : 0);
  groups = exponent > 0 ? 1 << exponent : 1;
  igroups = ie > 0 ? 1 << ie : 1;
  maximum =
      exponent >= 0 ? (512 / width) << exponent : (512 / width) >> (-exponent);
  lanes = form < 2 ? 1 : 64 / width;
  scalar = ((maximum - 1) & low_mask(64));
  for (int r = 0; r < groups; r++) {
    for (int c = 0; c < 8; c++) {
      model[8 + r][c] = random_word();
      model[24 + r][c] = random_word();
    }
    load_reg(8 + r);
    load_reg(24 + r);
  }
  for (int c = 0; c < 8; c++)
    model[0][c] = random_word();
  load_reg(0);
  if (form < 2) {
    for (int i = 0; i < maximum; i++) {
      idx = i % 5 == 0   ? ((maximum)&low_mask(64))
            : i % 5 == 1 ? ((maximum - 1) & low_mask(64))
            : i % 5 == 2 ? UINT64_MAX
                         : random_word() % ((maximum)&low_mask(64));
      row = i * iw / 64;
      offset = i * iw % 64;
      lane_mask = (UINT64_MAX >> (64 - iw)) << offset;
      model[16 + row / 8][row % 8] =
          (model[16 + row / 8][row % 8] & ~lane_mask) |
          ((idx << offset) & lane_mask);
    }
    for (int r = 0; r < igroups; r++)
      load_reg(16 + r);
  }
  for (int c = 0; c < groups * 8; c++)
    expected[c] = model[24 + c / 8][c % 8];
  for (int i = start; i < length; i++) {
    if (masked && element(0, i, 1) == 0)
      continue;
    idx = form < 2    ? element(16, i, iw)
          : form == 3 ? 31
                      : ((scalar)&low_mask(64));
    value = idx >= ((maximum)&low_mask(64)) ? 0 : element(8, int(idx), width);
    row = i * width / 64;
    offset = i * width % 64;
    lane_mask = (UINT64_MAX >> (64 - width)) << offset;
    expected[row] =
        (expected[row] & ~lane_mask) | ((value << offset) & lane_mask);
  }
  insn = vec(form == 1 ? 14 : 12, 24, 8,
             form < 2    ? 16
             : form == 3 ? 31
                         : 3,
             form < 2    ? 0
             : form == 2 ? 4
                         : 3,
             masked);
  run(insn, sew, lm, length, start, kill_after);
  written = kill_after < 0 ? length : (start / lanes + resolved_count) * lanes;
  for (int i = start; i < length && i < written; i++) {
    row = i * width / 64;
    offset = i * width % 64;
    lane_mask = (UINT64_MAX >> (64 - width)) << offset;
    model[24 + row / 8][row % 8] = (model[24 + row / 8][row % 8] & ~lane_mask) |
                                   (expected[row] & lane_mask);
  }
  for (int r = 0; r < groups; r++)
    check_reg(24 + r);
  if (kill_after >= 0) {
    // Restart precisely after the visible prefix, using the same index/data
    // sources; stale second-read context must never write across this edge.
    run(insn, sew, lm, length, written);
    for (int c = 0; c < groups * 8; c++)
      model[24 + c / 8][c % 8] = expected[c];
    for (int r = 0; r < groups; r++)
      check_reg(24 + r);
  }
}
void compress_case(int sew, int lm, int length, int pattern) {
  std::uint64_t expected[64];
  std::uint64_t value, lane_mask;
  int width, exponent, groups, output_index, row, offset;
  width = 8 << sew;
  exponent = lm < 4 ? lm : lm - 8;
  groups = exponent > 0 ? 1 << exponent : 1;
  for (int r = 0; r < groups; r++)
    for (int c = 0; c < 8; c++) {
      model[8 + r][c] = random_word();
      model[24 + r][c] = random_word();
      expected[r * 8 + c] = model[24 + r][c];
    }
  for (int c = 0; c < 8; c++)
    model[5][c] = pattern == 0 ? 0 : pattern == 1 ? UINT64_MAX : random_word();
  for (int r = 0; r < groups; r++) {
    load_reg(8 + r);
    load_reg(24 + r);
  }
  load_reg(5);
  output_index = 0;
  for (int i = 0; i < length; i++)
    if (element(5, i, 1) != 0) {
      value = element(8, i, width);
      row = output_index * width / 64;
      offset = output_index * width % 64;
      lane_mask = (UINT64_MAX >> (64 - width)) << offset;
      expected[row] =
          (expected[row] & ~lane_mask) | ((value << offset) & lane_mask);
      output_index++;
    }
  run(vec(23, 24, 8, 5, 2), sew, lm, length);
  for (int c = 0; c < groups * 8; c++)
    model[24 + c / 8][c % 8] = expected[c];
  for (int r = 0; r < groups; r++)
    check_reg(24 + r);
}

void drive() {
  // The sequencer's index can advance beyond the final row while idle.
  load_data = 0;
  if (mode == 1 && regno + int(wb_index) / 8 < 32)
    load_data = element(regno, int(wb_index), 64);
}

void observe() {}

void falling_update() {}

void stimulus() {
  reset = 1;
  request_valid = 0;
  issue_ready = 1;
  cancel = 0;
  instruction = 0;
  vtype = 0;
  vl = 0;
  vstart = 0;
  scalar = 0;
  saturate_count = 0;
  for (int i = 0; i < 3; ++i)
    tick();
  reset = 0;
  for (int r = 0; r < 32; r++) {
    for (int c = 0; c < 8; c++)
      model[r][c] = random_word();
    load_reg(r);
  }

  // Exercise all eight words of a register and one saturation pulse per beat.
  for (int c = 0; c < 8; c++) {
    model[8][c] = UINT64_MAX;
    model[9][c] = UINT64_MAX;
  }
  load_reg(8);
  load_reg(9);
  int prior_saturations = saturate_count;
  run(vec(32, 24, 8, 9, 0), 0, 0, 64);
  CHECK(saturate_count - prior_saturations == 8);
  for (int c = 0; c < 8; c++)
    model[24][c] = UINT64_MAX;
  check_reg(24);
  prior_saturations = saturate_count;
  run(vec(46, 24, 8, 0, 3), 0, 0, 64);
  CHECK(saturate_count - prior_saturations == 16);

  // The 128-bit fixture owns the full SEW/LMUL and aliasing sweeps. Here the
  // narrowest/widest elements traverse an eight-register source group.
  for (int sew : {0, 3}) {
    int width = 8 << sew;
    int length = 512 / width * 8;
    std::uint64_t mask = low_mask(width);
    for (int op = 0; op < 8; op++) {
      for (int masked = 0; masked < 2; masked++) {
        for (int c = 0; c < 8; c++)
          model[0][c] = random_word();
        load_reg(0);
        std::uint64_t acc = element(3, 0, width);
        for (int i = 0; i < length; i++)
          if (!masked || element(0, i, 1) != 0)
            acc = fold(op, width, acc, element(8, i, width));
        run(vec(op, 7, 8, 3, 2, masked), sew, 3, length);
        model[7][0] = (model[7][0] & ~mask) | (acc & mask);
        check_reg(7);
      }
    }
  }
  // Keep the feed-forward, bubble-free reduction check at this width.
  std::uint64_t acc = element(3, 0, 64);
  for (int i = 0; i < 16; i++)
    acc += element(8, i, 64);
  run(vec(0, 7, 8, 3, 2), 3, 1, 16, 0, -1, 1);
  model[7][0] = acc;
  check_reg(7);
  for (int sew = 0; sew < 3; sew++) {
    int length = 512 / (8 << sew) * 8;
    widening_reduction_case(0, sew, 3, length, 24);
    widening_reduction_case(1, sew, 3, length, 24, 3, 8, 1);
  }
  widening_reduction_case(1, 0, 3, 512, 7, 3, 8, 0, 2);

  // Preserve the wide-mask regressions: first-set at bits 63, 64, and 511,
  // a scalar popcount of 512, and SEW8 prefix/index wrap beyond 255.
  for (int op = 0; op < 7; op++) {
    for (int pattern = 0; pattern < 6; pattern++) {
      scan_case(op, 0, 3, 512, pattern, pattern == 5);
      scan_case(op, 0, 3, 65, pattern, 1);
    }
    scan_case(op, 0, 3, 64, 2, 0);
    scan_case(op, 0, 3, 511, 4, 0);
    scan_case(op, 0, 3, 512, 7, 1, 1);
    scan_case(op, 0, 3, 1, 1, 1);
  }
  // Nonzero vstart preserves the architectural origin of vid indices.
  for (int sew = 0; sew < 4; sew++) {
    scan_case(6, sew, 3, 512 / (8 << sew) * 8, 7, 1, -1, 3);
    scan_case(6, sew, 3, 1, 7, 0, -1, 5);
  }

  // Representative multiword permutations retain stalls, masked tails, and
  // cancellation followed by nonzero-vstart restart without another sweep.
  for (int sew : {0, 3}) {
    int length = 512 >> sew;
    for (int form = 0; form < 6; form++) {
      bool down = form == 2 || form == 3 || form == 5;
      slide_case(form, sew, length, 0, 0, -1, down);
      slide_case(form, sew, length - 1, 1, 1, -1, down);
      slide_case(form, sew, length, 0, 1, 1, down);
    }
    for (int form = 0; form < 4; form++) {
      // vrgatherei16 with SEW8 would need EMUL16 at LMUL8.
      if (sew == 0 && form == 1)
        continue;
      gather_case(form, sew, 3, length, 0, 0);
      gather_case(form, sew, 3, length - 1, 1, 1);
    }
    compress_case(sew, 3, length, 2);
  }
  std::cout << "macros=" << macros << " cycles=" << cycles
            << " checks=" << checks << '\n';
  throw Finished{};
}

int main() {
  return run_test([] {
    cycle_limit = 1000000;
    try {
      stimulus();
      for (;;)
        rising();
    } catch (const Finished &) {
    }
  });
}
