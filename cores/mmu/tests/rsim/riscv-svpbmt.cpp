// Checks Svpbmt encoding, walk capture, TLB lifetime, and PMA-independent
// attribute resolution.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

std::uint64_t leaf(int kind, std::uint64_t ppn) {
  std::uint64_t return_value{};

  return (((kind)&low_mask(64)) << 61) | (((ppn)&low_mask(64)) << 10) |
         UINT64_C(207);

  return return_value;
}

void clear_tlb() {
  eval();
  invalidate = 1;
  tick_model();
  invalidate = 0;
  eval();
  CHECK(!hit && !probe_hit);
}

void start_walk(bool enable_pbmt) {
  eval();
  command_valid = 1;
  pbmte = enable_pbmt;
  eval();
  CHECK(command_ready);
  tick_model();
  command_valid = 0;

  pbmte = !enable_pbmt;
}

void supply_pte(std::uint64_t expected_address, std::uint64_t value) {
  int waited;
  waited = 0;
  while (!memory_valid && waited < 10) {
    tick_model();
    waited++;
  }
  CHECK(memory_valid && memory_address == expected_address);
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index) {
    tick_model();
    CHECK(memory_valid && memory_address == expected_address);
  }
  eval();
  memory_ready = 1;
  tick_model();
  memory_ready = 0;
  eval();
  pte = value;
  response_valid = 1;
  tick_model();
  response_valid = 0;
}

void finish_walk(bool expect_fault, bool expect_access_fault, int kind,
                 std::uint64_t expected_address) {
  CHECK(completed && fault == expect_fault &&
        access_fault == expect_access_fault);
  CHECK(result_pbmt == ((kind)&low_mask(2)));
  if (!expect_fault && !expect_access_fault)
    CHECK(result_address == expected_address);
  for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index) {
    tick_model();
    CHECK(completed && result_pbmt == ((kind)&low_mask(2)));
    CHECK(!hit);
  }
  eval();
  completion_ready = 1;
  tick_model();
  completion_ready = 0;
  eval();
  CHECK(hit == !(expect_fault || expect_access_fault));
  if (!expect_fault && !expect_access_fault) {
    CHECK(!tlb_fault && probe_hit && tlb_pbmt == ((kind)&low_mask(2)) &&
          probe_pbmt == ((kind)&low_mask(2)));
    CHECK(tlb_address == expected_address + UINT64_C(291));
    enabled = 0;
    eval();
    CHECK(hit && probe_hit && tlb_pbmt == 0 && probe_pbmt == 0 &&
          tlb_address == slice(lookup_address, 55, 0));
    enabled = 1;
  }
}

int main() {
  return run_test([] {
    reset = 1;
    command_valid = 0;
    pbmte = 0;
    cancel = 0;
    address = UINT64_C(16384);
    lookup_address = UINT64_C(16675);
    access = 1;
    memory_ready = 0;
    memory_fault = 0;
    response_valid = 0;
    pte = 0;
    completion_ready = 0;
    enabled = 1;
    invalidate = 0;
    attribute_pbmt = 0;
    envcfg = 0;
    command_valid = 0;
    pbmte = 0;
    cancel = 0;
    memory_ready = 0;
    memory_fault = 0;
    response_valid = 0;
    completion_ready = 0;
    enabled = 1;
    invalidate = 0;
    physical = {};
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    reset = 0;

    for (int en = 0; en < 2; en++)
      for (int kind = 0; kind < 4; kind++)
        for (int is_leaf = 0; is_leaf < 2; is_leaf++) {
          pbmte = ((en)&low_mask(1));
          pte = (((kind)&low_mask(64)) << 61) |
                (is_leaf != 0 ? UINT64_C(207) : UINT64_C(1));
          eval();
          CHECK(pte_valid ==
                (kind == 0 || (en != 0 && is_leaf != 0 && kind < 3)));
          for (int bit_index = 54; bit_index < 64; bit_index++)
            if (bit_index != 61 && bit_index != 62) {
              pte |= UINT64_C(1) << bit_index;
              eval();
              CHECK(!pte_valid);
              pte &= ~(UINT64_C(1) << bit_index);
            }
        }
    envcfg = UINT64_MAX;
    eval();
    CHECK(envcfg_fields == UINT64_C(4611686018427387904) &&
          disabled_fields == 0 && rv32_fields == 0);
    for (int mapped = 0; mapped < 2; mapped++)
      for (int device = 0; device < 2; device++)
        for (int kind = 0; kind < 3; kind++) {
          physical = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
          physical.pmapped = ((mapped)&low_mask(1));
          physical.pdevice = ((device)&low_mask(1));
          physical.pcacheable = device == 0;
          physical.pinstruction_ucacheable = device == 0;
          physical.pread_uidempotent = device == 0;
          attribute_pbmt = ((kind)&low_mask(2));
          eval();
          CHECK(
              attributes.pcacheable ==
                  (mapped != 0 && kind == 0 && device == 0) &&
              attributes.pinstruction_ucacheable ==
                  (mapped != 0 && kind == 0 && device == 0) &&
              attributes.pread_uidempotent ==
                  (mapped != 0 && (kind == 1 || (kind == 0 && device == 0))) &&
              attributes.pstrongly_uordered ==
                  (mapped != 0 && (kind == 2 || (kind == 0 && device != 0))) &&
              attributes.pfence_umemory ==
                  (mapped != 0 && (kind == 1 || device == 0)) &&
              attributes.pfence_uio ==
                  (mapped != 0 && (kind == 2 || device != 0)));
        }

    physical = {};
    physical.pmapped = 1;
    physical.preadable = 1;
    physical.pexecutable = 1;
    physical.pinstruction_ucacheable = 1;
    physical.pread_uidempotent = 1;
    attribute_pbmt = 0;
    eval();
    CHECK(!attributes.pcacheable && attributes.pinstruction_ucacheable);
    attribute_pbmt = 1;
    eval();
    CHECK(!attributes.pcacheable && !attributes.pinstruction_ucacheable);

    for (int kind = 0; kind < 3; kind++) {
      clear_tlb();
      start_walk(1);
      supply_pte(UINT64_C(4096), UINT64_C(2049));
      supply_pte(UINT64_C(8192), UINT64_C(3073));
      supply_pte(UINT64_C(12320), leaf(kind, UINT64_C(8)));
      finish_walk(0, 0, kind, UINT64_C(32768));
    }

    clear_tlb();
    start_walk(1);
    supply_pte(UINT64_C(4096), UINT64_C(2049));
    supply_pte(UINT64_C(8192), leaf(1, UINT64_C(512)));
    finish_walk(0, 0, 1, UINT64_C(2113536));
    clear_tlb();
    start_walk(1);
    supply_pte(UINT64_C(4096), leaf(2, UINT64_C(262144)));
    finish_walk(0, 0, 2, UINT64_C(1073758208));

    for (int kind = 1; kind < 4; kind++) {
      clear_tlb();
      start_walk(0);
      supply_pte(UINT64_C(4096), leaf(kind, UINT64_C(262144)));
      finish_walk(1, 0, 0, 0);
      clear_tlb();
      start_walk(1);
      supply_pte(UINT64_C(4096),
                 (((kind)&low_mask(64)) << 61) | UINT64_C(2049));
      finish_walk(1, 0, 0, 0);
    }
    clear_tlb();
    start_walk(1);
    supply_pte(UINT64_C(4096), leaf(3, UINT64_C(262144)));
    finish_walk(1, 0, 0, 0);
    clear_tlb();
    start_walk(1);
    eval();
    memory_ready = 1;
    memory_fault = 1;
    tick_model();
    memory_ready = 0;
    memory_fault = 0;
    finish_walk(0, 1, 0, 0);

    clear_tlb();
    start_walk(1);
    supply_pte(UINT64_C(4096), leaf(1, UINT64_C(262144)));
    eval();
    invalidate = 1;
    completion_ready = 1;
    tick_model();
    invalidate = 0;
    completion_ready = 0;
    eval();
    CHECK(!hit && !probe_hit);

    start_walk(1);
    supply_pte(UINT64_C(4096), leaf(2, UINT64_C(262144)));
    eval();
    cancel = 1;
    completion_ready = 1;
    tick_model();
    cancel = 0;
    completion_ready = 0;
    eval();
    CHECK(!completed && !hit);
  });
}
