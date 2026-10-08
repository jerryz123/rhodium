// Checks compact 64-KiB translations, invalid N encodings, permissions, and TLB
// lifetime.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int request_count{};
void sample_tick() {
  eval();
  if (!reset && memory_valid && memory_ready)
    ++request_count;
  tick_model();
}
constexpr std::uint64_t N = UINT64_C(9223372036854775808);
constexpr std::uint64_t VA = UINT64_C(305397760);
constexpr std::uint64_t PA = UINT64_C(2147483648);
constexpr std::uint64_t PPN = UINT64_C(524288);

void clear_tlb() {
  eval();
  invalidate = 1;
  sample_tick();
  invalidate = 0;
  eval();
  CHECK(!hit && !probe_hit);
}

void start_walk(std::uint64_t va) {
  eval();
  address = va;
  command_valid = 1;
  eval();
  CHECK(command_ready);
  sample_tick();
  command_valid = 0;
}

void supply(std::uint64_t expected_address, std::uint64_t value) {
  eval();
  CHECK(memory_valid && memory_address == expected_address);
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index) {
    sample_tick();
    CHECK(memory_valid && memory_address == expected_address);
  }
  eval();
  memory_ready = 1;
  sample_tick();
  memory_ready = 0;
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
    sample_tick();
  eval();
  pte = value;
  response_valid = 1;
  sample_tick();
  response_valid = 0;
}

void walk(std::uint64_t va, int level, std::uint64_t leaf) {
  start_walk(va);
  supply(UINT64_C(4096) + ((va >> 30) & 511) * 8,
         level == 2 ? leaf : UINT64_C(2049));
  if (level < 2)
    supply(UINT64_C(8192) + ((va >> 21) & 511) * 8,
           level == 1 ? leaf : UINT64_C(3073));
  if (level < 1)
    supply(UINT64_C(12288) + ((va >> 12) & 511) * 8, leaf);
}

void finish(bool bad, std::uint64_t pa = 0, std::uint8_t size = 1,
            std::uint8_t kind = 0, std::uint64_t base = PPN) {
  CHECK(completed && fault == bad && !access_fault);
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index) {
    if (!bad)
      CHECK(result_address == pa && result_size == size &&
            result_base_ppn == base && result_pbmt == kind);
    sample_tick();
    CHECK(completed);
  }
  eval();
  completion_ready = 1;
  sample_tick();
  completion_ready = 0;
}

void lookup(std::uint64_t va, std::uint64_t pa, std::uint8_t size = 1,
            std::uint8_t kind = 0) {
  lookup_address = va;
  eval();
  CHECK(hit && !tlb_fault && probe_hit && !probe_fault && tlb_address == pa &&
        probe_address == pa && tlb_size == size && tlb_pbmt == kind &&
        probe_pbmt == kind);
}

int main() {
  return run_test([] {
    reset = 1;
    command_valid = 0;
    cancel = 0;
    pbmte = 1;
    address = UINT64_C(65536);
    lookup_address = UINT64_C(65536);
    access = 1;
    privilege = 1;
    validation_level = 0;
    sum = 0;
    mxr = 0;
    memory_ready = 0;
    memory_fault = 0;
    response_valid = 0;
    pte = 0;
    completion_ready = 0;
    enabled = 1;
    invalidate = 0;
    command_valid = 0;
    cancel = 0;
    pbmte = 1;
    sum = 0;
    mxr = 0;
    memory_ready = 0;
    memory_fault = 0;
    response_valid = 0;
    completion_ready = 0;
    enabled = 1;
    invalidate = 0;
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      sample_tick();
    reset = 0;

    for (int level = 0; level < 3; level++)
      for (int lo = 0; lo < 16; lo++)
        for (int flags = 0; flags < 16; flags++) {
          validation_level = ((level)&low_mask(2));
          pte = N | (((lo)&low_mask(64)) << 10) | ((flags)&low_mask(64));
          eval();
          CHECK(pte_valid == (level == 0 && lo == 8 && (flags & 1) != 0 &&
                              ((flags & 2) != 0 || (flags & 8) != 0) &&
                              !((flags & 4) != 0 && (flags & 2) == 0)));
          CHECK(!disabled_pte_valid);
        }

    for (int first = 0; first < 16; first++) {
      int before_requests = request_count;
      clear_tlb();
      walk(VA + ((first * 4096 + UINT64_C(0x123)) & low_mask(64)), 0,
           N | (((PPN + 8) & low_mask(64)) << 10) |
               (((first % 3) & low_mask(64)) << 61) | UINT64_C(207));
      finish(0, PA + ((first * 4096 + UINT64_C(0x123)) & low_mask(56)), 1,
             ((first % 3) & low_mask(2)));
      for (int page = 0; page < 16; page++)
        for (int offset_index = 0; offset_index < 3; offset_index++) {
          int offset = offset_index == 0   ? 0
                       : offset_index == 1 ? UINT64_C(0x123)
                                           : UINT64_C(0xfff);
          lookup(VA + ((page * 4096 + offset) & low_mask(64)),
                 PA + ((page * 4096 + offset) & low_mask(56)), 1,
                 ((first % 3) & low_mask(2)));
        }
      CHECK(request_count == before_requests + 3);
      lookup_address = VA - 1;
      eval();
      CHECK(!hit && !probe_hit);
      lookup_address = VA + 65536;
      eval();
      CHECK(!hit && !probe_hit);
    }

    for (int lo = 0; lo < 16; lo++)
      if (lo != 8) {
        clear_tlb();
        walk(VA, 0,
             N | (((PPN + ((lo)&low_mask(44))) & low_mask(64)) << 10) |
                 UINT64_C(207));
        finish(1);
        lookup_address = VA;
        eval();
        CHECK(!hit);
      }
    for (int level = 0; level < 3; level++) {
      clear_tlb();
      walk(VA, level, N | (((PPN + 8) & low_mask(64)) << 10) | UINT64_C(1));
      finish(1);
      if (level != 0) {
        clear_tlb();
        walk(VA, level, N | (((PPN + 8) & low_mask(64)) << 10) | UINT64_C(207));
        finish(1);
      }
    }
    for (int bit_index = 54; bit_index < 61; bit_index++) {
      clear_tlb();
      walk(VA, 0,
           N | (((PPN + 8) & low_mask(64)) << 10) | (UINT64_C(1) << bit_index) |
               UINT64_C(207));
      finish(1);
    }

    clear_tlb();
    walk(VA, 0, N | (((PPN + 8) & low_mask(64)) << 10) | UINT64_C(143));
    finish(1);
    access = 2;
    clear_tlb();
    walk(VA, 0, N | (((PPN + 8) & low_mask(64)) << 10) | UINT64_C(79));
    finish(1);
    access = 1;
    clear_tlb();
    walk(VA, 0, N | (((PPN + 8) & low_mask(64)) << 10) | UINT64_C(67));
    finish(0, PA);
    lookup(VA + UINT64_C(0xffff), PA + UINT64_C(0xffff));
    access = 2;
    eval();
    CHECK(hit && tlb_fault && probe_hit && !probe_fault);
    access = 0;
    eval();
    CHECK(hit && tlb_fault);
    privilege = 0;
    access = 1;
    eval();
    CHECK(hit && tlb_fault && probe_fault);
    privilege = 1;
    clear_tlb();
    access = 0;
    walk(VA, 0, N | (((PPN + 8) & low_mask(64)) << 10) | UINT64_C(89));
    finish(1);
    privilege = 0;
    walk(VA, 0, N | (((PPN + 8) & low_mask(64)) << 10) | UINT64_C(89));
    finish(0, PA);
    access = 1;
    mxr = 1;
    lookup(VA, PA);
    mxr = 0;
    eval();
    CHECK(hit && tlb_fault);
    privilege = 1;
    mxr = 1;
    sum = 0;
    eval();
    CHECK(hit && tlb_fault);
    sum = 1;
    lookup(VA, PA);
    privilege = 1;
    mxr = 0;
    sum = 0;
    access = 1;

    for (int level = 0; level < 3; level++) {
      std::uint64_t offset = ((VA + UINT64_C(291)) & low_mask(56)) &
                             ((UINT64_C(1) << (12 + 9 * level)) - 1);
      std::uint8_t size = level == 0 ? 0 : level == 1 ? 2 : 3;
      clear_tlb();
      walk(VA + UINT64_C(291), level,
           (((PPN)&low_mask(64)) << 10) | UINT64_C(207));
      finish(0, PA + offset, size);
      lookup(VA + UINT64_C(291), PA + offset, size);
    }

    clear_tlb();
    for (int region = 0; region < 3; region++) {
      walk(VA + ((region * 65536) & low_mask(64)), 0,
           N | (((PPN + 8) & low_mask(64)) << 10) | UINT64_C(207));
      finish(0, PA);
    }
    lookup_address = VA;
    eval();
    CHECK(!hit);
    lookup(VA + 65536 + UINT64_C(0xfff), PA + UINT64_C(0xfff));
    lookup(VA + 131072 + UINT64_C(0xf000), PA + UINT64_C(0xf000));
    enabled = 0;
    eval();
    CHECK(hit && probe_hit && tlb_address == slice(lookup_address, 55, 0) &&
          tlb_pbmt == 0 && tlb_size == 0);
    enabled = 1;
    clear_tlb();
    walk(VA, 0, N | (((PPN + 8) & low_mask(64)) << 10) | UINT64_C(207));
    eval();
    completion_ready = 1;
    invalidate = 1;
    sample_tick();
    completion_ready = 0;
    invalidate = 0;
    lookup_address = VA;
    eval();
    CHECK(!hit);
    walk(VA, 0, N | (((PPN + 8) & low_mask(64)) << 10) | UINT64_C(207));
    eval();
    cancel = 1;
    completion_ready = 1;
    sample_tick();
    cancel = 0;
    completion_ready = 0;
    eval();
    CHECK(!hit && !completed);
  });
}
