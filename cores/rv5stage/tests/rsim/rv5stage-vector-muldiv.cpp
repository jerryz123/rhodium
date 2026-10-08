// Preserves the rv5stage-vector-muldiv cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"
using data_resp_bits_t =
    std::remove_cvref_t<decltype(data_access_in.presponse.pbits)>;
// Checks shared vector integer, fixed-point multiply, mul-div, move, mask, and permutation paths through memory signatures.

bool response_valid = 0;

bool reject_request = 1;

std::uint32_t response_word;

std::uint32_t program_words[16384];

std::uint64_t memory_words[8192];

std::uint64_t expected_data[8192], expected_address[8192];

int expected_width[8192];

int pc = 0, expected_count = 0, stores = 0, cycles = 0, load_delay = 0;

data_resp_bits_t pending_load;

void emit(std::uint32_t word) { program_words[pc++] = word; }
void li(int rd, int value) {
  emit((((((value + 2048) >> 12) << 12) | (rd << 7) | UINT64_C(55)) &
        low_mask(32)));
  emit(((((value & 4095) << 20) | (rd << 15) | (rd << 7) | UINT64_C(19)) &
        low_mask(32)));
}
void vset(int sew, int vl, int lmul = 0) {
  emit(((UINT64_C(0xc0007057) | (sew << 23) | (lmul << 20) | (vl << 15)) &
        low_mask(32)));
}
void vec(int funct6, int rd, int vs2, int vs1, std::uint8_t masked = 0,
         int funct3 = 2) {
  emit((((funct6 << 26) | (int(!masked) << 25) | (vs2 << 20) | (vs1 << 15) |
         (funct3 << 12) | (rd << 7) | UINT64_C(87)) &
        low_mask(32)));
}
void vload(int rd, int address, int width) {
  li(10, address);
  emit((
      (UINT64_C(0x2050007) | ((width == 0 ? 0 : width + 4) << 12) | (rd << 7)) &
      low_mask(32)));
}
void expect_store(int address, std::uint64_t value, int width) {
  expected_address[expected_count] = ((address)&low_mask(64));
  expected_data[expected_count] = value;
  expected_width[expected_count++] = width;
}
void vstore(int rd, int address, int width) {
  li(10, address);
  emit((
      (UINT64_C(0x2050027) | ((width == 0 ? 0 : width + 4) << 12) | (rd << 7)) &
      low_mask(32)));
}
void signature(int csr, int address, std::uint64_t value) {
  emit((
      ((csr << 20) | UINT64_C(8691)) &
      low_mask(32))); // csrr x3,csr; all state observers must drain vector work
  li(10, address);
  emit(UINT64_C(0x353023)); // sd x3,0(x10)
  expect_store(address, value, 3);
}
void write_vector_csr(int csr, int value) {
  emit((((csr << 20) | (value << 15) | UINT64_C(20595)) & low_mask(32)));
}

// Independent integer oracle; no iteration or RTL selection logic is mirrored.
std::uint64_t arithmetic(int code, int sew, std::uint64_t left,
                         std::uint64_t right) {
  std::uint64_t mask, a, b;
  signed128 sa, sb, product;
  int width;
  width = 8 << sew;
  mask = UINT64_MAX >> (64 - width);
  a = left & mask;
  b = right & mask;
  sa = signed128(a);
  sb = signed128(b);
  if ((code == UINT64_C(33) || code == UINT64_C(35) || code == UINT64_C(38) ||
       code == UINT64_C(39)) &&
      bit_slice(a, width - 1, 1))
    sa -= (signed128(1) << width);
  if ((code == UINT64_C(33) || code == UINT64_C(35) || code == UINT64_C(39)) &&
      bit_slice(b, width - 1, 1))
    sb -= (signed128(1) << width);
  product = signed128(uint128(sa) * uint128(sb));
  switch (code) {
  case UINT64_C(32): {
    return b == 0 ? mask : (a / b);
  } break;
  case UINT64_C(33): {
    return b == 0 ? mask : (((sa / sb) & low_mask(64)) & mask);
  } break;
  case UINT64_C(34): {
    return b == 0 ? a : (a % b);
  } break;
  case UINT64_C(35): {
    return b == 0 ? a : (((sa % sb) & low_mask(64)) & mask);
  } break;
  case UINT64_C(37): {
    return ((product)&low_mask(64)) & mask;
  } break;
  default: {
    return ((product >> width) & low_mask(64)) & mask;
  } break;
  }
}
std::uint64_t fractional_multiply(int sew, int mode, std::uint64_t left,
                                  std::uint64_t right) {
  std::uint64_t mask, a, b;
  signed128 sa, sb, product, shifted;
  uint128 discarded_mask;
  bool round_bit, lower_nonzero, discarded_nonzero, increment;
  int width, distance;
  width = 8 << sew;
  distance = width - 1;
  mask = UINT64_MAX >> (64 - width);
  a = left & mask;
  b = right & mask;
  sa = signed128(a);
  sb = signed128(b);
  if (bit_slice(a, width - 1, 1))
    sa -= signed128(1) << width;
  if (bit_slice(b, width - 1, 1))
    sb -= signed128(1) << width;
  product = signed128(uint128(sa) * uint128(sb));
  shifted = product >> distance;
  round_bit = bit_slice(product, distance - 1, 1);
  discarded_mask = (uint128(1) << distance) - 1;
  lower_nonzero = (product & (discarded_mask >> 1)) != 0;
  discarded_nonzero = (product & discarded_mask) != 0;
  switch (mode) {
  case 0: {
    increment = round_bit;
  } break;
  case 1: {
    increment = round_bit && (lower_nonzero || bit_slice(shifted, 0, 1));
  } break;
  case 2: {
    increment = 0;
  } break;
  case 3: {
    increment = !bit_slice(shifted, 0, 1) && discarded_nonzero;
  } break;
  }
  if (a == (UINT64_C(1) << (width - 1)) && b == (UINT64_C(1) << (width - 1)))
    return mask >> 1;
  return ((shifted + ((increment)&low_mask(128))) & low_mask(64)) & mask;
}
std::uint64_t left_value(int lane, int sew) {
  std::uint64_t sign_bit;
  sign_bit = UINT64_C(1) << ((8 << sew) - 1);
  switch (lane % 8) {
  case 0: {
    return sign_bit;
  } break;
  case 1: {
    return UINT64_MAX;
  } break;
  case 2: {
    return sign_bit - 1;
  } break;
  case 3: {
    return 0;
  } break;
  case 4: {
    return 17;
  } break;
  case 5: {
    return -UINT64_C(29);
  } break;
  case 6: {
    return UINT64_C(0xfedcba9876543210);
  } break;
  default: {
    return UINT64_C(0x123456789abcdef);
  } break;
  }
}
std::uint64_t reduce_value(int op, int width, std::uint64_t a,
                           std::uint64_t b) {
  std::int64_t sa, sb;
  sa = std::int64_t(sign_extend(a << (64 - width), 64)) >> (64 - width);
  sb = std::int64_t(sign_extend(b << (64 - width), 64)) >> (64 - width);
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
    return sa < sb ? a : b;
  } break;
  case 6: {
    return a > b ? a : b;
  } break;
  default: {
    return sa > sb ? a : b;
  } break;
  }
}
std::uint64_t widening_value(int op, int width, std::uint64_t a,
                             std::uint64_t b) {
  std::uint64_t source_mask, left_mask, destination_mask;
  std::int64_t signed_a, signed_b;
  source_mask = UINT64_MAX >> (64 - width);
  destination_mask = UINT64_MAX >> (64 - 2 * width);
  left_mask = op >= UINT64_C(52) ? destination_mask : source_mask;
  signed_a =
      std::int64_t(sign_extend(
          (a & left_mask) << (64 - (op >= UINT64_C(52) ? 2 * width : width)),
          64)) >>
      (64 - (op >= UINT64_C(52) ? 2 * width : width));
  signed_b = std::int64_t(sign_extend((b & source_mask) << (64 - width), 64)) >>
             (64 - width);
  if (bit_slice(op, 0, 1))
    return (bit_slice(op, 1, 1)
                ? std::uint64_t(signed_a) - std::uint64_t(signed_b)
                : std::uint64_t(signed_a) + std::uint64_t(signed_b)) &
           destination_mask;
  return (bit_slice(op, 1, 1) ? (a & left_mask) - (b & source_mask)
                              : (a & left_mask) + (b & source_mask)) &
         destination_mask;
}
std::uint64_t widening_multiply_value(int op, int width, std::uint64_t a,
                                      std::uint64_t b) {
  std::uint64_t source_mask, destination_mask;
  signed128 signed_a, signed_b, product;
  source_mask = UINT64_MAX >> (64 - width);
  destination_mask = UINT64_MAX >> (64 - 2 * width);
  signed_a = signed128(a & source_mask);
  signed_b = signed128(b & source_mask);
  if (op != UINT64_C(56) && bit_slice(a, width - 1, 1))
    signed_a -= signed128(1) << width;
  if (op == UINT64_C(59) && bit_slice(b, width - 1, 1))
    signed_b -= signed128(1) << width;
  product = signed128(uint128(signed_a) * uint128(signed_b));
  return ((product)&low_mask(64)) & destination_mask;
}
std::uint64_t multiply_accumulate_value(int op, int width, std::uint64_t a,
                                        std::uint64_t b, std::uint64_t addend) {
  std::uint64_t source_mask, destination_mask;
  signed128 signed_a, signed_b, product, result;
  source_mask = UINT64_MAX >> (64 - width);
  destination_mask =
      op >= UINT64_C(60) ? UINT64_MAX >> (64 - 2 * width) : source_mask;
  signed_a = signed128(a & source_mask);
  signed_b = signed128(b & source_mask);
  if ((op == UINT64_C(61) || op == UINT64_C(62)) && bit_slice(a, width - 1, 1))
    signed_a -= signed128(1) << width;
  if ((op == UINT64_C(61) || op == UINT64_C(63)) && bit_slice(b, width - 1, 1))
    signed_b -= signed128(1) << width;
  product = signed128(uint128(signed_a) * uint128(signed_b));
  switch (op) {
  case UINT64_C(41): {
    result = signed128(uint128(signed_a) +
                       uint128(signed_b) * uint128(addend & source_mask));
  } break;
  case UINT64_C(43): {
    result = signed128(uint128(signed_a) -
                       uint128(signed_b) * uint128(addend & source_mask));
  } break;
  case UINT64_C(47): {
    result = signed128(uint128(addend & source_mask) - uint128(product));
  } break;
  default: {
    result = signed128(uint128(addend & destination_mask) + uint128(product));
  } break;
  }
  return ((result)&low_mask(64)) & destination_mask;
}
std::uint64_t right_value(int lane) {
  switch (lane % 8) {
  case 0: {
    return UINT64_MAX; // minimum / -1, plus mixed-sign high multiplication
  } break;
  case 1: {
    return 0; // divide by zero
  } break;
  case 2: {
    return 3;
  } break;
  case 3: {
    return 0;
  } break;
  case 4: {
    return -UINT64_C(7);
  } break;
  case 5: {
    return 11;
  } break;
  case 6: {
    return UINT64_C(0x8000000080000080);
  } break;
  default: {
    return UINT64_MAX;
  } break;
  }
}
void memory_element(int address, std::uint64_t value, int sew) {
  for (int b = 0; b < (1 << sew); b++)
    bit_slice(memory_words[(address - UINT64_C(0x10000) + b) >> 3],
              8 * ((address + b) & 7), 8) = bit_slice(value, 8 * b, 8);
}
void scalar_signature(int rd, int address, std::uint64_t value) {
  li(10, address);
  emit((((rd << 20) | UINT64_C(0x53023)) & low_mask(32)));
  expect_store(address, value, 3);
}

void drive() {
  {
    instruction_access_in = {};
    instruction_access_in.prequest.pready =
        instruction_access_out.pflush || !response_valid;
    instruction_access_in.presponse.pvalid = response_valid;
    instruction_access_in.presponse.pbits.pword = response_word;
    data_access_in = {};
    // Reject once, then hold readiness through replay instead of phase-locking
    // a periodic ready waveform against the core's fixed replay latency.
    data_access_in.prequest.pready = load_delay == 0 && !reject_request;
    data_access_in.presponse.pvalid = load_delay == 1;
    data_access_in.presponse.pbits = pending_load;
    data_access_in.pdrained = load_delay == 0;
  }
}

void observe() {
  {
    if (!reset) {
      defer(cycles, cycles + 1);
      if (data_access_out.prequest.pvalid)
        defer(reject_request, data_access_in.prequest.pready);
      if (load_delay > 1 ||
          (load_delay == 1 && data_access_out.presponse.pready))
        defer(load_delay, load_delay - 1);
      if (instruction_access_out.pflush ||
          (response_valid && instruction_access_out.presponse.pready))
        defer(response_valid, 0);
      if (instruction_access_out.prequest.pvalid &&
          instruction_access_in.prequest.pready) {
        defer(response_valid, 1);
        CHECK(instruction_access_out.prequest.pbits.paddress <
              UINT64_C(0x10000));
        defer(response_word,
              program_words[bit_slice(
                  instruction_access_out.prequest.pbits.paddress, 2, 14)]);
      }
      if (data_access_out.prequest.pvalid && data_access_in.prequest.pready) {
        // The shared LSU returns a tagged completion for stores as well as loads.
        defer(pending_load.paccess_ufault, 0);
        defer(pending_load.pcontext.pwriteback,
              data_access_out.prequest.pbits.pcontext.pwriteback);
        defer(pending_load.pdata, 0);
        defer(load_delay, 3 + cycles % 4);
        if (data_access_out.prequest.pbits.paccess == 1) {
          CHECK(data_access_out.prequest.pbits.paddress >= UINT64_C(0x10000) &&
                data_access_out.prequest.pbits.paddress < UINT64_C(0x20000));
          defer(pending_load.pdata,
                memory_words[(((data_access_out.prequest.pbits.paddress -
                                UINT64_C(0x10000)) >>
                               3) &
                              low_mask(13))] >>
                    (8 * (data_access_out.prequest.pbits.paddress & 7)));
        } else {
          CHECK(stores < expected_count &&
                data_access_out.prequest.pbits.paccess == 2);
          CHECK(data_access_out.prequest.pbits.paddress ==
                    expected_address[stores] &&
                int(data_access_out.prequest.pbits.pwidth) ==
                    expected_width[stores] &&
                (data_access_out.prequest.pbits.pdata &
                 (UINT64_C(0xffffffffffffffff) >>
                  (64 - (8 << expected_width[stores])))) ==
                    expected_data[stores]);
          defer(stores, stores + 1);
          if (stores + 1 == expected_count) {

            throw Finished{};
          }
        }
      }
      if (cycles > 400000)
        fail(1, "vector muldiv timeout stores=%0d fetch=%h", stores,
             instruction_access_out.prequest.pbits.paddress);
    }
  }
}

void falling_update() {}

void stimulus() {
  reset = 1;
  interrupts = {};
  hart_id = 0;
  time_counter = 0;
  {
    int address, left_address, right_address, width, code, compressed_count;
    std::uint64_t mask, a, b;
    std::uint16_t compress_mask;
    for (int i = 0; i < 16384; i++)
      program_words[i] = UINT64_C(111);
    for (int i = 0; i < 8192; i++)
      memory_words[i] = 0;
    compress_mask = UINT64_C(42330);
    memory_words[(((UINT64_C(0x1e000) - UINT64_C(0x10000)) >> 3) &
                  low_mask(13))] = ((compress_mask)&low_mask(64));
    for (int i = 0; i < 16; i++) {
      memory_element(UINT64_C(0x1e080) + i, ((i + 1) & low_mask(64)), 0);
      memory_element(UINT64_C(0x1e100) + i, UINT64_C(238), 0);
    }
    li(1, UINT64_C(65280));
    emit(UINT64_C(0x30509073));
    li(1, UINT64_C(512));
    emit(UINT64_C(0x30009073)); // VS Initial, no scalar FP required
    address = UINT64_C(0x20000);
    for (int sew = 0; sew < 4; sew++) {
      left_address = UINT64_C(0x10000) + sew * 256;
      right_address = left_address + 128;
      width = 8 << sew;
      mask = UINT64_MAX >> (64 - width);
      for (int lane = 0; lane < 16; lane++) {
        memory_element(left_address + (lane << sew), left_value(lane, sew),
                       sew);
        memory_element(right_address + (lane << sew), right_value(lane), sew);
      }
      vset(sew, 16, 3);
      vload(8, left_address, sew);
      vload(16, right_address, sew);
      for (int op = 0; op < 8; op++) {
        code = UINT64_C(32) + op;
        for (int vx = 0; vx < 2; vx++) {
          li(5, -3);
          li(6, 7);
          vec(code, 24, 8, vx != 0 ? 5 : 16, 0, vx != 0 ? 6 : 2);
          // Younger scalar operations contend with the accepted vector tail.
          // Changing x5 also checks that VX captured its original scalar operand.
          li(5, 9);
          emit(UINT64_C(0x26283b3)); // mul x7,x5,x6 = 63
          emit(UINT64_C(0x262ceb3)); // div x29,x5,x6 = 1
          emit(UINT64_C(1123));
          emit(UINT64_C(0x27283b3)); // taken branch kills younger multiply
          scalar_signature(7, address, 63);
          address += 8;
          scalar_signature(29, address, 1);
          address += 8;
          vstore(24, address, sew);
          for (int lane = 0; lane < 16; lane++)
            expect_store(address + (lane << sew),
                         arithmetic(code, sew, left_value(lane, sew),
                                    vx != 0 ? -UINT64_C(3) : right_value(lane)),
                         sew);
          address += 128;
        }
      }
      // Fractional multiply reuses the shared integer multiplier, then
      // rounds its full product and reports delayed saturation at ordered drain.
      for (int round_mode = 0; round_mode < 4; round_mode++) {
        write_vector_csr(UINT64_C(9), 0);
        write_vector_csr(UINT64_C(10), round_mode);
        vec(UINT64_C(39), 24, 8, 8, 0, 0);
        vstore(24, address, sew);
        for (int lane = 0; lane < 16; lane++)
          expect_store(address + (lane << sew),
                       fractional_multiply(sew, round_mode,
                                           left_value(lane, sew),
                                           left_value(lane, sew)),
                       sew);
        address += 128;
        signature(UINT64_C(9), address, 1);
        address += 8;
        write_vector_csr(UINT64_C(9), 0);
        write_vector_csr(UINT64_C(10), round_mode);
        li(5, -3);
        vec(UINT64_C(39), 24, 8, 5, 0, 4);
        vstore(24, address, sew);
        for (int lane = 0; lane < 16; lane++)
          expect_store(address + (lane << sew),
                       fractional_multiply(sew, round_mode,
                                           left_value(lane, sew), -UINT64_C(3)),
                       sew);
        address += 128;
        signature(UINT64_C(9), address, 0);
        address += 8;
      }
      // The third general VRF port supplies old vd while the dedicated v0
      // shadow remains available for predication. VMADD/VNMSUB instead use vd
      // as a multiplicand and retain vs2 as the addend.
      for (int operation = 0; operation < 4; operation++) {
        code = UINT64_C(41) + 2 * operation;
        for (int vx = 0; vx < 2; vx++) {
          vload(24, right_address, sew);
          li(5, -3);
          vec(code, 24, 8, vx != 0 ? 5 : 16, 0, vx != 0 ? 6 : 2);
          vstore(24, address, sew);
          for (int lane = 0; lane < 16; lane++)
            expect_store(address + (lane << sew),
                         multiply_accumulate_value(
                             code, width, left_value(lane, sew),
                             vx != 0 ? -UINT64_C(3) : right_value(lane),
                             right_value(lane)),
                         sew);
          address += 128;
        }
      }
      vload(24, right_address, sew);
      vec(UINT64_C(31), 0, 8, 0, 0, 3);
      emit(UINT64_C(0x81d073));
      vec(UINT64_C(45), 24, 8, 16, 1, 2);
      vstore(24, address, sew);
      for (int lane = 0; lane < 16; lane++) {
        a = left_value(lane, sew) & mask;
        b = right_value(lane) & mask;
        expect_store(
            address + (lane << sew),
            lane >= 3 && a != 0 && !bit_slice(a, width - 1, 1)
                ? multiply_accumulate_value(UINT64_C(45), width, a, b, b)
                : b,
            sew);
      }
      address += 128;
      signature(UINT64_C(8), address, 0);
      address += 8;
      // In-place, masked, restarted operations preserve disabled and prestart lanes.
      emit(UINT64_C(1123));
      vec(UINT64_C(37), 8, 8, 16, 0,
          2); // older branch squashes vector execution before WB
      vec(UINT64_C(31), 0, 8, 0, 0, 3); // vmsgt.vi v0,v8,0
      emit(UINT64_C(0x81d073));         // vstart=3
      vec(UINT64_C(38), 8, 8, 16, 1, 2);
      vstore(8, address, sew);
      for (int lane = 0; lane < 16; lane++) {
        a = left_value(lane, sew) & mask;
        b = right_value(lane) & mask;
        expect_store(address + (lane << sew),
                     lane >= 3 && a != 0 && !bit_slice(a, width - 1, 1)
                         ? arithmetic(UINT64_C(38), sew, a, b)
                         : a,
                     sew);
      }
      address += 128;
      signature(UINT64_C(8), address, 0);
      address += 8;
      // An all-masked operation still drains its ordered completion slots.
      vec(UINT64_C(25), 0, 8, 8, 0, 0);
      vec(UINT64_C(33), 8, 8, 16, 1, 2);
      vstore(8, address, sew);
      for (int lane = 0; lane < 16; lane++) {
        a = left_value(lane, sew) & mask;
        b = right_value(lane) & mask;
        expect_store(address + (lane << sew),
                     lane >= 3 && a != 0 && !bit_slice(a, width - 1, 1)
                         ? arithmetic(UINT64_C(38), sew, a, b)
                         : a,
                     sew);
      }
      address += 128;
      // VL=0 plus nonzero vstart emits exactly one empty macro completion.
      vset(sew, 0, 3);
      emit(UINT64_C(0x83d073));
      vec(UINT64_C(39), 24, 8, 16, 0, 2);
      signature(UINT64_C(8), address, 0);
      address += 8;
    }
    // Narrow-source widening uses the ordinary Decode/sequencer/WB path and
    // stores through doubled EEW/EMUL. A taken branch must squash a younger op.
    for (int sew = 0; sew < 3; sew++) {
      width = 8 << sew;
      vset(sew, 16, 2);
      vload(8, UINT64_C(0x10000) + sew * 256, sew);
      vload(16, UINT64_C(0x10080) + sew * 256, sew);
      for (int op = UINT64_C(48); op <= UINT64_C(51); op++) {
        for (int vx = 0; vx < 2; vx++) {
          li(5, -3);
          vec(op, 24, 8, vx != 0 ? 5 : 16, 0, vx != 0 ? 6 : 2);
          emit(UINT64_C(1123));
          vec(op ^ 2, 24, 16, vx != 0 ? 5 : 8, 0, vx != 0 ? 6 : 2);
          vstore(24, address, sew + 1);
          for (int lane = 0; lane < 16; lane++)
            expect_store(
                address + (lane << (sew + 1)),
                widening_value(op, width, left_value(lane, sew),
                               vx != 0 ? -UINT64_C(3) : right_value(lane)),
                sew + 1);
          address += 128;
        }
      }
    }
    // Widening accumulate retains a doubled-width old destination while both
    // narrow multiplicands continue through the shared scalar multiplier.
    for (int sew = 0; sew < 3; sew++) {
      int wide_address;
      width = 8 << sew;
      wide_address = UINT64_C(0x11000) + sew * 256;
      vset(sew, 16, 2);
      vload(8, UINT64_C(0x10000) + sew * 256, sew);
      vload(16, UINT64_C(0x10080) + sew * 256, sew);
      for (int operation = 0; operation < 4; operation++) {
        code = UINT64_C(60) + operation;
        for (int vx = (code == UINT64_C(62) ? 1 : 0); vx < 2; vx++) {
          vload(24, wide_address, sew + 1);
          li(5, -3);
          vec(code, 24, 8, vx != 0 ? 5 : 16, 0, vx != 0 ? 6 : 2);
          vstore(24, address, sew + 1);
          for (int lane = 0; lane < 16; lane++)
            expect_store(address + (lane << (sew + 1)),
                         multiply_accumulate_value(
                             code, width, left_value(lane, sew),
                             vx != 0 ? -UINT64_C(3) : right_value(lane),
                             left_value(lane, sew + 1)),
                         sew + 1);
          address += 128;
        }
      }
    }
    // Widening multiply keeps source SEW in the shared request while its
    // completion writes a doubled-width destination element and EMUL group.
    for (int sew = 0; sew < 3; sew++) {
      width = 8 << sew;
      vset(sew, 16, 2);
      vload(8, UINT64_C(0x10000) + sew * 256, sew);
      vload(16, UINT64_C(0x10080) + sew * 256, sew);
      for (int operation = 0; operation < 3; operation++) {
        code = operation == 0 ? UINT64_C(56) : UINT64_C(57) + operation;
        for (int vx = 0; vx < 2; vx++) {
          li(5, -3);
          vec(code, 24, 8, vx != 0 ? 5 : 16, 0, vx != 0 ? 6 : 2);
          li(5, 9);
          emit(UINT64_C(1123));
          vec(code, 24, 16, vx != 0 ? 5 : 8, 0, vx != 0 ? 6 : 2);
          vstore(24, address, sew + 1);
          for (int lane = 0; lane < 16; lane++)
            expect_store(address + (lane << (sew + 1)),
                         widening_multiply_value(
                             code, width, left_value(lane, sew),
                             vx != 0 ? -UINT64_C(3) : right_value(lane)),
                         sew + 1);
          address += 128;
        }
      }
    }
    // Fractional source LMUL widens into one full destination register.
    for (int sew = 0; sew < 3; sew++) {
      int element_count;
      width = 8 << sew;
      element_count = 8 >> sew;
      vset(sew, element_count, 7);
      vload(8, UINT64_C(0x10000) + sew * 256, sew);
      vload(16, UINT64_C(0x10080) + sew * 256, sew);
      vec(UINT64_C(58), 24, 8, 16, 0, 2);
      vstore(24, address, sew + 1);
      for (int lane = 0; lane < element_count; lane++)
        expect_store(address + (lane << (sew + 1)),
                     widening_multiply_value(UINT64_C(58), width,
                                             left_value(lane, sew),
                                             right_value(lane)),
                     sew + 1);
      address += 128;
    }
    // Wide-source widening reads vs2 at the destination EEW while retaining
    // a narrow vector/scalar second operand and the ordinary WB beat schedule.
    for (int sew = 0; sew < 3; sew++) {
      int wide_address;
      width = 8 << sew;
      wide_address = UINT64_C(0x11000) + sew * 256;
      for (int lane = 0; lane < 16; lane++)
        memory_element(wide_address + (lane << (sew + 1)),
                       left_value(lane, sew + 1), sew + 1);
      vset(sew, 16, 2);
      vload(8, wide_address, sew + 1);
      vload(16, UINT64_C(0x10080) + sew * 256, sew);
      for (int op = UINT64_C(52); op <= UINT64_C(55); op++) {
        for (int wx = 0; wx < 2; wx++) {
          li(5, -3);
          vec(op, 24, 8, wx != 0 ? 5 : 16, 0, wx != 0 ? 6 : 2);
          vstore(24, address, sew + 1);
          for (int lane = 0; lane < 16; lane++)
            expect_store(
                address + (lane << (sew + 1)),
                widening_value(op, width, left_value(lane, sew + 1),
                               wx != 0 ? -UINT64_C(3) : right_value(lane)),
                sew + 1);
          address += 128;
        }
      }
    }
    // Exercise the new cheap operations through Decode and real WB, not just
    // the standalone sequencer. Older branches must squash each new family.
    for (int sew = 0; sew < 4; sew++) {
      width = 8 << sew;
      mask = UINT64_MAX >> (64 - width);
      vset(sew, 16, 3);
      vload(8, UINT64_C(0x10000) + sew * 256, sew);
      vload(16, UINT64_C(0x10080) + sew * 256, sew);
      for (int form = 0; form < 3; form++) {
        li(5, -7);
        vec(UINT64_C(23), 24, 0,
            form == 0   ? 16
            : form == 1 ? 5
                        : 29,
            0,
            form == 0   ? 0
            : form == 1 ? 4
                        : 3);
        li(5, 13); // the broadcast must have captured -7, not this value
        emit(UINT64_C(1123));
        vec(UINT64_C(23), 24, 0, 0, 0, 3);
        vstore(24, address, sew);
        for (int lane = 0; lane < 16; lane++)
          expect_store(address + (lane << sew),
                       (form == 0   ? right_value(lane)
                        : form == 1 ? -UINT64_C(7)
                                    : -UINT64_C(3)) &
                           mask,
                       sew);
        address += 128;
        // Selection is data: the zero half of the mask still writes vs2.
        vec(UINT64_C(31), 0, 8, 0, 0, 3);
        emit(UINT64_C(0x81d073));
        li(5, -9);
        vec(UINT64_C(23), 24, 8,
            form == 0   ? 16
            : form == 1 ? 5
                        : 17,
            1,
            form == 0   ? 0
            : form == 1 ? 4
                        : 3);
        emit(UINT64_C(1123));
        vec(UINT64_C(23), 24, 16, 0, 1, 3);
        vstore(24, address, sew);
        for (int lane = 0; lane < 16; lane++) {
          a = left_value(lane, sew) & mask;
          b = lane < 3 ? (form == 0   ? right_value(lane)
                          : form == 1 ? -UINT64_C(7)
                                      : -UINT64_C(3))
              : a != 0 && !bit_slice(a, width - 1, 1)
                  ? (form == 0   ? right_value(lane)
                     : form == 1 ? -UINT64_C(9)
                                 : -UINT64_C(15))
                  : a;
          expect_store(address + (lane << sew), b & mask, sew);
        }
        address += 128;
      }
    }
    // Mask logic uses 128 one-bit elements even though its registers are
    // unaligned for the current LMUL=8. Read back both physical mask words.
    for (int op = 24; op < 32; op++) {
      std::uint64_t expected;
      vset(3, 2);
      vload(3, UINT64_C(0x10000), 3);
      vload(5, UINT64_C(0x10080), 3);
      vload(7, UINT64_C(0x10100), 3);
      li(1, 128);
      emit(UINT64_C(0x30f057)); // vsetvli x0,x1,e8,m8
      li(1, 63);
      emit(UINT64_C(0x809073)); // vstart straddles mask words
      vec(op, 3, 3, 5, 0, 2);
      emit(UINT64_C(1123));
      vec(27, 3, 3, 3, 0, 2);
      vset(3, 2);
      vstore(3, address, 3);
      for (int word_index = 0; word_index < 2; word_index++) {
        a = memory_words[word_index];
        b = memory_words[16 + word_index];
        switch (op) {
        case 24: {
          expected = a & ~b;
        } break;
        case 25: {
          expected = a & b;
        } break;
        case 26: {
          expected = a | b;
        } break;
        case 27: {
          expected = a ^ b;
        } break;
        case 28: {
          expected = a | ~b;
        } break;
        case 29: {
          expected = ~(a & b);
        } break;
        case 30: {
          expected = ~(a | b);
        } break;
        default: {
          expected = ~(a ^ b);
        } break;
        }
        if (word_index == 0)
          expected = (field(bit_slice(expected, 63, 1), 1, 63) |
                      field(bit_slice(a, 0, 63), 63, 0));
        expect_store(address + word_index * 8, expected, 3);
      }
      address += 16;
      signature(UINT64_C(8), address, 0);
      address += 8;
    }
    for (int sew = 0; sew < 4; sew++) {
      std::uint64_t accumulator, scalar_value;
      width = 8 << sew;
      mask = UINT64_MAX >> (64 - width);
      for (int op = 0; op < 8; op++) {
        vset(sew, 2);
        vload(3, UINT64_C(0x10080) + sew * 256, sew);
        vload(7, UINT64_C(0x10080) + sew * 256, sew);
        vset(sew, 16, 3);
        vload(8, UINT64_C(0x10000) + sew * 256, sew);
        vec(UINT64_C(31), 0, 8, 0, 0, 3); // positive source elements define v0
        accumulator = mask;
        for (int i = 0; i < 16; i++) {
          a = left_value(i, sew) & mask;
          if (op % 2 == 0 || (a != 0 && !bit_slice(a, width - 1, 1)))
            accumulator = reduce_value(op, width, accumulator, a);
        }
        vec(op, 7, 8, 3, op % 2 != 0, 2);
        vec(16, 5, 7, 0, 0, 2);   // vmv.x.s x5,v7, with an immediate dependent
        emit(UINT64_C(0x128313)); // addi x6,x5,1
        scalar_value =
            ((std::int64_t(sign_extend(accumulator << (64 - width), 64)) >>
              (64 - width)) &
             low_mask(64));
        scalar_signature(6, address, scalar_value + 1);
        address += 8;
        vset(sew, 2);
        vstore(7, address, sew);
        expect_store(address, accumulator, sew);
        expect_store(address + (1 << sew), 0, sew);
        address += 16;
        signature(UINT64_C(8), address, 0);
        address += 8;
      }
      // Moves ignore LMUL alignment, and only extraction ignores empty bodies.
      vset(sew, 2);
      vload(7, UINT64_C(0x10080) + sew * 256, sew);
      vset(sew, 16, 3);
      li(5, -9);
      vec(16, 7, 0, 5, 0, 6); // vmv.s.x
      emit(UINT64_C(1123));
      vec(16, 7, 0, 0, 0, 6); // squashed insertion
      vset(sew, 0, 3);
      vec(16, 7, 0, 0, 0, 6); // empty insertion
      emit(UINT64_C(0x83d073));
      vec(16, 5, 7, 0, 0, 2); // extraction with vl=0,vstart=7
      emit(UINT64_C(0x128313));
      scalar_signature(6, address, -UINT64_C(8));
      address += 8;
      signature(UINT64_C(8), address, 0);
      address += 8;
      vset(sew, 2);
      vstore(7, address, sew);
      expect_store(address, -UINT64_C(9) & mask, sew);
      expect_store(address + (1 << sew), 0, sew);
      address += 16;
      vset(sew, 0, 3);
      vec(0, 7, 8, 3, 0, 2); // empty reduction must not copy seed
      vec(16, 5, 7, 0, 0, 2);
      scalar_signature(5, address, -UINT64_C(9));
      address += 8;
      vec(16, 0, 7, 0, 0, 2);
      scalar_signature(0, address, 0);
      address += 8;
      li(5, 7);
      emit(UINT64_C(1123));
      vec(16, 5, 7, 0, 0, 2);
      scalar_signature(5, address, 7);
      address += 8;
      // An older deferred GPR writer must drain before vmv.x.s overwrites it.
      li(5, 7);
      li(6, 13);
      emit(UINT64_C(0x26282b3));
      vec(16, 5, 7, 0, 0, 2);
      scalar_signature(5, address, -UINT64_C(9));
      address += 8;
    }
    // Nonzero vstart is illegal for every reduction, even with VL=0. The trap
    // handler skips the instruction; a sentinel catches unintended VRF writes.
    for (int op = 0; op < 8; op++) {
      vset(0, op % 2 == 0 ? 8 : 0);
      emit(UINT64_C(0x80d073));
      vec(op, 7, 8, 3, 0, 2);
      expect_store(UINT64_C(0x2fff0), 2, 3);
      vec(16, 5, 7, 0, 0, 2);
      scalar_signature(5, address, -UINT64_C(9));
      address += 8;
    }
    // Queries share normal GPR retirement; prefixes and indices use packed VRF writes.
    {
      std::uint64_t source_mask, prefix;
      int count, first_set;
      source_mask = 0;
      count = 0;
      first_set = -1;
      vset(0, 16);
      vload(8, UINT64_C(0x10000), 0);
      vec(UINT64_C(31), 0, 8, 0, 0, 3);
      for (int i = 0; i < 16; i++) {
        a = left_value(i, 0) & 255;
        if (a != 0 && !bit_slice(a, 7, 1)) {
          bit_slice(source_mask, i, 1) = 1;
          count++;
          if (first_set < 0)
            first_set = i;
        }
      }
      for (int masked = 0; masked < 2; masked++) {
        for (int query = 0; query < 2; query++) {
          li(5, 7);
          li(6, 13);
          emit(UINT64_C(0x26282b3)); // older deferred WAW
          vec(16, 5, 0, 16 + query, ((masked)&low_mask(1)), 2);
          emit(UINT64_C(0x128313));
          scalar_signature(6, address,
                           (query == 0 ? ((count)&low_mask(64))
                                       : ((first_set)&low_mask(64))) +
                               UINT64_C(1));
          address += 8;
        }
      }
      vec(16, 0, 0, 16, 0, 2);
      scalar_signature(0, address, 0);
      address += 8;
      li(5, 9);
      emit(UINT64_C(1123));
      vec(16, 5, 0, 16, 0, 2);
      scalar_signature(5, address, 9);
      address += 8;
      for (int op = 0; op < 3; op++) {
        vset(0, 16);
        vec(20, 3, 0, op == 0 ? 1 : op == 1 ? 3 : 2, 0, 2);
        prefix = 0;
        for (int i = 0; i < 16; i++)
          bit_slice(prefix, i, 1) = op == 0   ? first_set < 0 || i < first_set
                                    : op == 1 ? first_set < 0 || i <= first_set
                                              : i == first_set;
        vset(0, 2);
        vstore(3, address, 0);
        expect_store(address, prefix & 255, 0);
        expect_store(address + 1, (prefix >> 8) & 255, 0);
        address += 8;
      }
      for (int op = 0; op < 2; op++) {
        count = 0;
        vset(0, 16);
        vec(20, 16, 0, 16 + op, 0, 2);
        vstore(16, address, 0);
        for (int i = 0; i < 16; i++) {
          expect_store(address + i, ((op == 0 ? count : i) & low_mask(64)), 0);
          if (bit_slice(source_mask, i, 1))
            count++;
        }
        address += 16;
      }
      vset(0, 0);
      for (int query = 0; query < 2; query++) {
        vec(16, 5, 0, 16 + query, 0, 2);
        scalar_signature(5, address, query == 0 ? 0 : UINT64_MAX);
        address += 8;
      }
      for (int op = 0; op < 6; op++) {
        int selector;
        selector = op == 0   ? 16
                   : op == 1 ? 17
                   : op == 2 ? 1
                   : op == 3 ? 3
                   : op == 4 ? 2
                             : 16;
        vset(0, op % 2 == 0 ? 16 : 0);
        emit(UINT64_C(0x80d073));
        vec(op < 2 ? 16 : 20, op < 2 ? 5 : 16, 0, selector, 0, 2);
        expect_store(UINT64_C(0x2fff0), 2, 3);
        signature(UINT64_C(8), address, 0);
        address += 8;
      }
    }
    // Slides use captured scalar operands and survive scalar-LSU backpressure.
    // Initialize beyond VL so slidedown must read source elements past VL.
    for (int sew = 0; sew < 4; sew++) {
      for (int form = 0; form < 6; form++) {
        int dest, mode;
        bool up, one, masked;
        std::uint64_t value;
        up = (form == 0 || form == 1 || form == 4);
        one = form >= 4;
        masked = bit_slice(form, 0, 1);
        dest = up ? 16 : 8;
        mode = one ? 6 : (form == 1 || form == 3) ? 3 : 4;
        width = 8 << sew;
        mask = UINT64_MAX >> (64 - width);
        vset(sew, 16, 3);
        vload(8, UINT64_C(0x10000) + sew * 256, sew);
        vload(16, UINT64_C(0x10080) + sew * 256, sew);
        vec(30, 0, 8, 0, 0, 3); // vmsgtu.vi v0,v8,0
        vset(sew, 13, 3);
        li(5, one ? -19 : 3);
        if (mode == 4) {
          li(5, 1);
          li(6, 3);
          emit(UINT64_C(0x26282b3));
        } // deferred scalar producer
        emit(UINT64_C(
            8441971)); // vstart=1 preserves the first destination element
        vec(up ? 14 : 15, dest, 8, mode == 3 ? 3 : 5, masked, mode);
        emit(UINT64_C(1123));
        vec(15, dest, 8, 5, 0,
            4); // squashed slide must preserve the observed destination
        signature(UINT64_C(8), address, 0);
        address += 8;
        vset(sew, 16, 3);
        vstore(dest, address, sew);
        for (int i = 0; i < 16; i++) {
          value = up ? right_value(i) : left_value(i, sew);
          if (i >= 1 && i < 13 &&
              (!masked || (left_value(i, sew) & mask) != 0) &&
              (!up || one || i >= 3)) {
            if (one && i == (up ? 0 : 12))
              value = -UINT64_C(19);
            else
              value =
                  left_value(up ? i - (one ? 1 : 3) : i + (one ? 1 : 3), sew);
          }
          expect_store(address + (i << sew), value & mask, sew);
        }
        address += 128;
      }
      vset(sew, 0, 3);
      vec(14, 8, 8, 0, 0, 4);
      expect_store(UINT64_C(0x2fff0), 2, 3); // reserved overlap even with VL=0
    }
    // Gather index producers, deferred scalar index dependencies, nonzero
    // restart, masking, and squash all cross the real scalar/vector boundary.
    for (int sew = 0; sew < 4; sew++)
      for (int form = 0; form < 4; form++) {
        std::uint64_t value;
        int iw;
        width = 8 << sew;
        mask = UINT64_MAX >> (64 - width);
        iw = form == 1 ? 1 : sew;
        vset(iw, 8, 2);
        vec(20, 24, 0, 17, 0, 2); // vid.v supplies indices 0..7
        vset(sew, 8, 2);
        vload(8, UINT64_C(0x10000) + sew * 256, sew);
        vload(16, UINT64_C(0x10080) + sew * 256, sew);
        vec(30, 0, 8, 0, 0, 3);
        vset(sew, 7, 2);
        emit(UINT64_C(0x80d073)); // preserve destination element zero
        li(5, 1);
        li(6, 7);
        emit(UINT64_C(0x26282b3)); // gather must wait for deferred x5=7
        vec(form == 1 ? 14 : 12, 16, 8,
            form < 2    ? 24
            : form == 3 ? 7
                        : 5,
            bit_slice(form, 0, 1),
            form < 2    ? 0
            : form == 2 ? 4
                        : 3);
        emit(UINT64_C(1123));
        vec(12, 16, 8, 0, 0, 4); // a squashed gather must not splat source zero
        signature(UINT64_C(8), address, 0);
        address += 8;
        vset(sew, 8, 2);
        vstore(16, address, sew);
        for (int i = 0; i < 8; i++) {
          value = right_value(i);
          if (i >= 1 && i < 7 &&
              (!bit_slice(form, 0, 1) || (left_value(i, sew) & mask) != 0))
            value = left_value(form < 2 ? i : 7, sew);
          expect_store(address + (i << sew), value & mask, sew);
        }
        address += 128;
        vset(sew, 0, 2);
        vec(12, 8, 8, 0, 0, 4);
        expect_store(UINT64_C(0x2fff0), 2, 3);
      }
    // vcompress.vm packs selected source elements, preserves destination tails,
    // traps on nonzero vstart, and remains squashable before its first WB beat.
    vset(3, 2);
    vload(5, UINT64_C(0x1e000), 3);
    vset(0, 16);
    vload(8, UINT64_C(0x1e080), 0);
    vload(24, UINT64_C(0x1e100), 0);
    vec(23, 24, 8, 5, 0, 2);
    vstore(24, address, 0);
    compressed_count = 0;
    for (int i = 0; i < 16; i++)
      if (bit_slice(compress_mask, i, 1)) {
        expect_store(address + compressed_count, ((i + 1) & low_mask(64)), 0);
        compressed_count++;
      }
    for (int i = compressed_count; i < 16; i++)
      expect_store(address + i, UINT64_C(238), 0);
    address += 16;
    vload(24, UINT64_C(0x1e100), 0);
    emit(UINT64_C(1123));
    vec(23, 24, 8, 5, 0, 2);
    vstore(24, address, 0);
    for (int i = 0; i < 16; i++)
      expect_store(address + i, UINT64_C(238), 0);
    address += 16;
    li(1, 1);
    emit(UINT64_C(0x809073));
    vec(23, 24, 8, 5, 0, 2);
    expect_store(UINT64_C(0x2fff0), 2, 3);
    vstore(24, address, 0);
    for (int i = 0; i < 16; i++)
      expect_store(address + i, UINT64_C(238), 0);
    address += 16;
    signature(UINT64_C(8), address, 0);
    address += 8;
    vset(0, 0);
    vec(23, 8, 8, 5, 0, 2);
    expect_store(UINT64_C(0x2fff0), 2, 3);
    CHECK(pc < UINT64_C(65280) / 4);
    pc = UINT64_C(65280) / 4;
    emit(UINT64_C(0x342021f3));
    li(10, UINT64_C(0x2fff0));
    emit(UINT64_C(0x353023));
    emit(UINT64_C(0x341021f3));
    emit(UINT64_C(0x418193));
    emit(UINT64_C(0x34119073));
    emit(UINT64_C(0x801073));
    emit(UINT64_C(0x30200073));
    for (int repeat_index = 0; repeat_index < (4); ++repeat_index)
      rising();
    falling();
    reset = 0;
  }
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
