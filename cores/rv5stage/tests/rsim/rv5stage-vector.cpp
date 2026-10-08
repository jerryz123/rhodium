// Preserves the rv5stage-vector cycle and architectural oracle through direct rsim.
// SPDX-License-Identifier: Apache-2.0
#include "driver.hpp"

// Differentially checks vector storage, v0 mask-shadow reads, forwarding, packing, and SIMD writes.

std::uint64_t memory[256];
std::uint64_t pending_data, pending_mask;
std::uint64_t pending_reads[3], pending_mask_read;
int pending_address;
bool pending_valid, pending_legal;
int bank_index, vlen_bits, depth, chunks;
std::int64_t checks = 0;
std::uint64_t rng = UINT64_C(0x146b35e7da08c912);

std::uint64_t random_word() {
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return rng;
}

void step() {
  std::uint64_t a, b, c, m, x, y, value, lane_mask, data, mask_bits,
      broadcast_value;
  std::uint64_t actual_data, actual_mask;
  int width_bits, out_width, lanes, first, source_lane, position, address;
  bool legal, enabled, expected_valid;
  settle();
  if (pending_valid) {
    CHECK(read_a_result[bank_index] == pending_reads[0] &&
          read_b_result[bank_index] == pending_reads[1] &&
          read_c_result[bank_index] == pending_reads[2] &&
          mask_read_result[bank_index] == pending_mask_read);
  }
  if (!reset) {
    if (commit && pending_valid && pending_legal)
      memory[pending_address] = (memory[pending_address] & ~pending_mask) |
                                (pending_data & pending_mask);
    else if (write_valid)
      memory[int(write_address) % depth] =
          (memory[int(write_address) % depth] & ~write_mask) |
          (write_data & write_mask);
  }
  a = memory[int(read_a) % depth];
  b = memory[int(read_b) % depth];
  c = memory[int(read_c) % depth];
  m = memory[int(mask_read_address) % chunks];
  width_bits = 8 << element_width;
  out_width = widening ? width_bits * 2 : width_bits;
  lanes = 64 / width_bits;
  first = int(first_element) + ((widening && upper_half) ? lanes / 2 : 0);
  address = int(destination) * chunks +
            (mask_destination ? first / 64 : first * out_width / 64);
  legal = int(first_element) % lanes == 0 && vl <= vlmax && vlmax != 0 &&
          int(vlmax) <= vlen_bits && first_element < vlmax &&
          !(widening && width_bits == 64) && address < depth &&
          (!mask_destination || first < vlen_bits);
  data = 0;
  mask_bits = 0;
  broadcast_value = scalar;
  if (operand_select == 2) {
    switch (immediate_kind) {
    case 1: {
      broadcast_value =
          (field(UINT64_C(0), 59, 5) | field(bit_slice(immediate, 0, 5), 5, 0));
    } break;
    case 2: {
      broadcast_value = (field(UINT64_C(0), 58, 6) | field(immediate, 6, 0));
    } break;
    default: {
      broadcast_value = sign_extend(immediate, 5);
    } break;
    }
  }
  if (out_width <= 64) {
    lane_mask = UINT64_C(0xffffffffffffffff) >> (64 - width_bits);
    for (int lane = 0; lane < 64 / out_width; lane++) {
      source_lane = lane + ((widening && upper_half) ? lanes / 2 : 0);
      position = first + lane;
      enabled = position >= int(vstart) && position < int(vl) &&
                position < int(vlmax) &&
                (!masked || bit_slice(m, position % 64, 1));
      x = left_wide ? (a >> (lane * out_width)) &
                          (UINT64_C(0xffffffffffffffff) >> (64 - out_width))
                    : (a >> (source_lane * width_bits)) & lane_mask;
      y = operand_select == 0 ? (b >> (source_lane * width_bits)) & lane_mask
                              : broadcast_value & lane_mask;
      if (widening && !left_wide && left_signed &&
          bit_slice(x, width_bits - 1, 1))
        x |= ~lane_mask;
      if (widening && right_signed && bit_slice(y, width_bits - 1, 1))
        y |= ~lane_mask;
      switch (operation) {
      case 0: {
        value = x + y;
      } break;
      case 1: {
        value = x - y;
      } break;
      case 2: {
        value = x ^ y;
      } break;
      case 3: {
        value = (field(UINT64_C(0), 63, 64) | field(x < y, 64, 0));
      } break;
      case 4: {
        value = x << int(y & ((out_width - 1) & low_mask(64)));
      } break;
      default: {
        value = x + y;
      } break;
      }
      value &= UINT64_C(0xffffffffffffffff) >> (64 - out_width);
      if (enabled) {
        if (mask_destination) {
          if (x < y)
            data |= UINT64_C(1) << (position % 64);
          mask_bits |= UINT64_C(1) << (position % 64);
        } else {
          data |= value << (lane * out_width);
          mask_bits |= (UINT64_C(0xffffffffffffffff) >> (64 - out_width))
                       << (lane * out_width);
        }
      }
    }
  }
  expected_valid = read_valid && !reset;
  rising();
  settle();
  CHECK(result_valid[bank_index] == expected_valid);
  if (expected_valid) {
    CHECK(read_a_result[bank_index] == a && read_b_result[bank_index] == b &&
          read_c_result[bank_index] == c && mask_read_result[bank_index] == m);
    CHECK(result_legal[bank_index] == legal);
    if (legal) {
      actual_data = result_data[bank_index];
      actual_mask = result_mask[bank_index];
      CHECK(int(result_address[bank_index]) == address && actual_data == data &&
            actual_mask == mask_bits);
    }
    checks++;
  }
  pending_data = data;
  pending_mask = mask_bits;
  pending_address = address;
  pending_reads[0] = a;
  pending_reads[1] = b;
  pending_reads[2] = c;
  pending_mask_read = m;
  pending_valid = expected_valid;
  pending_legal = legal;
  falling();
}

void defaults() {
  read_valid = 0;
  write_valid = 0;
  commit = 0;
  read_a = 0;
  read_b = 0;
  read_c = 0;
  mask_read_address = 0;
  write_address = 0;
  write_data = 0;
  write_mask = 0;
  destination = 0;
  mask_destination = 0;
  element_width = 0;
  first_element = 0;
  vl = 0;
  vstart = 0;
  vlmax = 1;
  masked = 0;
  operand_select = 0;
  immediate_kind = 0;
  widening = 0;
  upper_half = 0;
  left_wide = 0;
  left_signed = 0;
  right_signed = 0;
  scalar = 0;
  immediate = 0;
  operation = 0;
}

void drive() {}

void observe() {}

void falling_update() {}

void stimulus() {
  reset = 1;
  {
    defaults();
    bank_index = 0;
    vlen_bits = 128;
    depth = 64;
    chunks = 2;
    pending_valid = 0;
    pending_legal = 0;
    for (int i = 0; i < 256; i++)
      memory[i] = 0;
    step();
    reset = 0;
    for (int config_index = 0; config_index < 3; config_index++) {
      bank_index = config_index;
      vlen_bits = 128 << config_index;
      pending_valid = 0;
      chunks = vlen_bits / 64;
      depth = 32 * chunks;
      defaults();
      // Initialize every physical chunk, including writable v0 and v31.
      for (int address = 0; address < depth; address++) {
        write_valid = 1;
        write_address = ((address)&low_mask(16));
        write_data = random_word();
        write_mask = UINT64_MAX;
        step();
      }
      write_valid = 0;
      read_valid = 1;
      vlmax = ((vlen_bits)&low_mask(17));
      vl = ((vlen_bits)&low_mask(17));
      for (int address = 0; address < depth; address++) {
        read_a = ((address)&low_mask(16));
        read_b = (((address + 1) % depth) & low_mask(16));
        read_c = (((address + depth - 1) % depth) & low_mask(16));
        mask_read_address = ((address % chunks) & low_mask(16));
        step();
      }
      // All general ports and the mask shadow collide with v0 masked writes;
      // disjoint updates accumulate and forward through both physical copies.
      read_a = 0;
      read_b = 0;
      read_c = 0;
      mask_read_address = 0;
      write_address = 0;
      write_valid = 1;
      for (int bit_index = 0; bit_index < 64; bit_index++) {
        write_mask = UINT64_C(1) << bit_index;
        write_data = random_word();
        step();
      }
      write_valid = 0;
      // Read-before-write snapshots make in-place vector operations safe.
      // Contiguous chunks cross a register boundary in an LMUL=2 group.
      for (int width_index = 0; width_index < 4; width_index++) {
        element_width = ((width_index)&low_mask(2));
        vlmax = ((2 * vlen_bits / (8 << width_index)) & low_mask(17));
        vl = vlmax - 1;
        vstart = 1;
        destination = 4;
        commit = 1;
        for (int chunk = 0; chunk < 2 * chunks; chunk++) {
          first_element = ((chunk * (8 >> width_index)) & low_mask(17));
          read_a = ((4 * chunks + chunk) & low_mask(16));
          read_b = ((8 * chunks + chunk) & low_mask(16));
          read_c = ((16 * chunks + chunk) & low_mask(16));
          mask_read_address = ((first_element / 64) & low_mask(16));
          step();
        }
        read_valid = 0;
        step();
        commit = 0;
        read_valid = 1;
      }
      // Legal widening overlap: the narrow source is the upper register of
      // the destination group. Ascending source chunks preserve unread data.
      element_width = 0;
      widening = 1;
      masked = 0;
      operation = 0;
      vlmax = ((vlen_bits / 8) & low_mask(17));
      vl = vlmax - 1;
      vstart = 1;
      destination = 4;
      commit = 1;
      for (int chunk = 0; chunk < chunks; chunk++) {
        first_element = ((chunk * 8) & low_mask(17));
        read_a = ((5 * chunks + chunk) & low_mask(16));
        read_b = ((8 * chunks + chunk) & low_mask(16));
        upper_half = 0;
        step();
        upper_half = 1;
        step();
      }
      read_valid = 0;
      step();
      commit = 0;
      read_valid = 1;
      widening = 0;
      // Compare bits in successive mask chunks without overwriting neighbors.
      element_width = 0;
      operation = 3;
      mask_destination = 1;
      masked = 1;
      vlmax = ((vlen_bits)&low_mask(17));
      vl = vlmax - 3;
      vstart = 5;
      destination = 31;
      commit = 1;
      for (int element = 0; element < vlen_bits; element += 8) {
        first_element = ((element)&low_mask(17));
        read_a = ((8 * chunks + element / 8) & low_mask(16));
        read_b = ((16 * chunks + element / 8) & low_mask(16));
        read_c = ((24 * chunks + element / 8) & low_mask(16));
        mask_read_address = ((element / 64) & low_mask(16));
        step();
      }
      read_valid = 0;
      step();
      commit = 0;
      read_valid = 1;
      // A fully masked body emits no writes, even when explicitly committed.
      write_valid = 1;
      write_address = 0;
      write_data = 0;
      write_mask = UINT64_MAX;
      read_c = 0;
      mask_read_address = 0;
      first_element = 0;
      vstart = 0;
      vl = vlmax;
      step();
      CHECK(result_mask[bank_index] == 0);
      write_valid = 0;
      commit = 1;
      step();
      commit = 0;
      // Fractional LMUL, empty body, all masked, broadcasts, widening halves.
      for (int trial = 0; trial < 1600; trial++) {
        element_width = ((trial % 4) & low_mask(2));
        widening = trial % 5 == 0 && element_width != 3;
        left_wide = widening && (trial & 32) != 0;
        upper_half = (trial & 1) != 0;
        left_signed = (trial & 8) != 0;
        right_signed = (trial & 16) != 0;
        vlmax = ((vlen_bits / (8 << element_width)) & low_mask(17));
        if (trial % 7 == 0 && element_width == 0)
          vlmax = ((vlen_bits / 64) & low_mask(17));
        first_element = (((trial % int(vlmax)) / (8 >> element_width) *
                          (8 >> element_width)) &
                         low_mask(17));
        vl = ((trial % (int(vlmax) + 1)) & low_mask(17));
        vstart = (((trial * 3) % (int(vlmax) + 1)) & low_mask(17));
        masked = (trial & 2) != 0;
        mask_destination = (trial % 9) == 0;
        operand_select = ((trial % 3) & low_mask(2));
        immediate_kind = ((trial % 3) & low_mask(2));
        immediate = ((trial)&low_mask(6));
        scalar = random_word();
        operation = ((trial % 5) & low_mask(3));
        destination = ((trial % 24) & low_mask(5));
        read_a = ((trial % depth) & low_mask(16));
        read_b = (((trial * 13) % depth) & low_mask(16));
        read_c = (((trial * 7) % chunks) & low_mask(16));
        mask_read_address = read_c;
        commit = (trial % 3) != 0;
        read_valid = (trial % 17) != 0;
        write_valid = !commit;
        write_address = read_a;
        write_mask = random_word();
        write_data = random_word();
        step();
      }
      // Reset cancels a read result and blocks writes without clearing storage.
      write_valid = 1;
      commit = 0;
      read_valid = 1;
      write_address = 0;
      write_mask = UINT64_MAX;
      write_data = 0;
      reset = 1;
      step();
      reset = 0;
      write_valid = 0;
      read_a = 0;
      step();
      // Invalid chunk alignment, unsupported widening, and bank overflow.
      defaults();
      read_valid = 1;
      vlmax = ((vlen_bits)&low_mask(17));
      vl = vlmax;
      first_element = 1;
      step();
      first_element = 0;
      element_width = 3;
      widening = 1;
      step();
      widening = 0;
      destination = 31;
      first_element = ((vlen_bits / 64) & low_mask(17));
      step();
    }

    throw Finished{};
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
