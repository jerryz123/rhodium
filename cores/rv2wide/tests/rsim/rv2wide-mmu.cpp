// Checks speculative and authorized translations, invalidation, faults, and split accesses.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void falling() {
  eval();
  eval();
}
void offer_wb(std::uint64_t va) {
  falling();
  address = va;
  wb_valid = 1;
  tick_model();
  falling();
  wb_valid = 0;
}
void wait_request(std::uint64_t pa) {
  eval(); // Settle a just-withdrawn WB offer before observing the next PTE request.
  for (int i = 0; !physical_valid && i < 80; i++)
    tick_model();
  CHECK(physical_valid && physical_address == pa);
  falling();
}
void return_data(std::uint64_t data, bool core_reply = 0) {
  response_valid = 1;
  response_data = data;
  eval();
  CHECK(response_ready && completed == core_reply);
  if (core_reply)
    CHECK(completed_data == data);
  tick_model();
  falling();
  response_valid = 0;
}
void reply(std::uint64_t pa, std::uint64_t data) {
  wait_request(pa);
  CHECK(physical_locality == 0 && physical_pbmt == 0);
  physical_ready = 1;
  tick_model();
  falling();
  physical_ready = 0;
  return_data(data);
}
void fence() {
  falling();
  invalidate = 1;
  tick_model();
  falling();
  invalidate = 0;
  tick_model();
  falling();
}
void start_split(std::uint64_t va, int operation = 1, int width = 3) {
  falling();
  address = va;
  access = ((operation)&low_mask(4));
  split_width = ((width)&low_mask(2));
  split_valid = 1;
  eval();
  CHECK(split_ready);
  tick_model();
  falling();
  split_valid = 0;
}
void finish_split(std::uint64_t value, std::uint64_t fault_va = 0,
                  bool page = 0, bool access_error = 0) {
  for (int i = 0; !split_completed && i < 80; i++)
    tick_model();
  CHECK(split_completed && split_page_fault == page &&
        split_access_fault == access_error);
  if (page || access_error)
    CHECK(split_fault_address == fault_va);
  else
    CHECK(split_result == value);
  CHECK(!completed);
  tick_model();
  falling();
}
void walk_data(std::uint64_t leaf) {
  reply(UINT64_C(0x10000), (UINT64_C(17) << 10) | 1);
  reply(UINT64_C(0x11010), (UINT64_C(18) << 10) | 1);
  reply(UINT64_C(0x12800), leaf);
  for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
    tick_model();
  falling();
}
void walk_at(std::uint64_t va, std::uint64_t leaf, int level = 0) {
  reply(UINT64_C(0x10000) + ((va >> 30) & 511) * 8,
        level == 2 ? leaf : (UINT64_C(17) << 10) | 1);
  if (level < 2)
    reply(UINT64_C(0x11000) + ((va >> 21) & 511) * 8,
          level == 1 ? leaf : (UINT64_C(18) << 10) | 1);
  if (level < 1)
    reply(UINT64_C(0x12000) + ((va >> 12) & 511) * 8, leaf);
  for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
    tick_model();
  falling();
}
void data_hit(std::uint64_t va, std::uint64_t pa, int operation = 1) {
  address = va;
  access = ((operation)&low_mask(4));
  ex_valid = 1;
  tick_model();
  falling();
  ex_valid = 0;
  eval();
  CHECK(result_valid && resolve_valid && resolve_address == pa &&
        !physical_valid);
  tick_model();
  falling();
  wb_valid = 1;
  eval();
  CHECK(physical_valid && physical_address == pa && physical_access == access &&
        !wb_fault);
  // No acceptance: inspect the authorization path without allocating a reply.
  wb_valid = 0;
}
void data_page_fault(std::uint64_t va, int operation = 1) {
  address = va;
  access = ((operation)&low_mask(4));
  wb_valid = 1;
  eval();
  CHECK(wb_fault && !physical_valid && !wb_ready &&
        wb_fault_bits.pcause == (operation == 1 ? 13 : 15) &&
        wb_fault_bits.pvalue == va);
  tick_model();
  falling();
  wb_valid = 0;
}
void hint(std::uint64_t va, int operation, bool accepted, std::uint64_t pa = 0,
          int cancel_stage = 0) {
  // Two registered stages; neither a dropped hint nor a successful probe walks.
  falling();
  prefetch_in = {1, {va, std::uint8_t(operation & 3)}};
  tick_model();
  falling();
  prefetch_in = {};
  if (cancel_stage == 1)
    invalidate = 1;
  tick_model();
  falling();
  if (cancel_stage == 2)
    invalidate = 1;
  if (cancel_stage == 3)
    mstatus = mstatus ^ (UINT64_C(1) << 18);
  eval();
  CHECK(physical_prefetch_out.pvalid == accepted && !physical_valid &&
        !completed && !wb_fault);
  if (accepted)
    CHECK(physical_prefetch_out.pbits.paddress == pa &&
          physical_prefetch_out.pbits.poperation == ((operation)&low_mask(2)));
  tick_model();
  falling();
  invalidate = 0;
  for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index) {
    tick_model();
    CHECK(!physical_valid && !physical_prefetch_out.pvalid);
  }
  falling();
}
int main() {
  return run_test([] {
    reset = 1;
    privilege = 1;
    satp = UINT64_C(0x8000000000000010);
    mstatus = 0;
    pbmte = 0;
    guest_access = 0;
    invalidate = 0;
    fetch_valid = 0;
    ex_valid = 0;
    wb_valid = 0;
    commit = 0;
    access = 1;
    locality = 0;
    fetch_address = UINT64_C(0x400000);
    address = UINT64_C(0x500008);
    physical_ready = 0;
    physical_fault = 0;
    response_valid = 0;
    response_data = 0;
    split_valid = 0;
    split_width = 3;
    split_data = UINT64_C(0x8877665544332211);
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      tick_model();
    falling();
    reset = 0;
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    for (int operation = 1; operation <= 3; operation++)
      hint(UINT64_C(0x50003f), operation, 0);
    privilege = 3;
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    for (int operation = 1; operation <= 3; operation++)
      hint(UINT64_C(0x83), operation, 1, UINT64_C(0x80));
    hint(UINT64_C(0x100000000000083), 2,
         0); // Bare PA overflow must not alias a mapped line.
    hint(UINT64_C(0x3000), 2, 0); // Unmapped.
    hint(UINT64_C(0x2000), 2, 0); // Device.
    hint(UINT64_C(0x2300), 2, 0); // Noncacheable.
    hint(UINT64_C(0x2400), 3, 0); // Region does not contain a complete block.
    hint(UINT64_C(0x2500), 1, 0); // Cacheable but not executable.
    hint(UINT64_C(0x83), 2, 0, 0, 1);
    hint(UINT64_C(0x83), 2, 0, 0, 2);
    hint(UINT64_C(0x83), 2, 0, 0, 3);
    privilege = 1;
    mstatus = 0;
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    falling();
    // Cold EX indexing is unconditional, but a speculative miss never walks.
    ex_valid = 1;
    eval();
    CHECK(index_valid && index_address == UINT64_C(0x500008) &&
          !resolve_valid && !result_valid);
    tick_model();
    falling();
    ex_valid = 0;
    eval();
    CHECK(result_valid && outcome == 0 && !resolve_valid);
    for (unsigned repeat_index = 0; repeat_index < (8); ++repeat_index) {
      tick_model();
      CHECK(!physical_valid);
    }
    offer_wb(UINT64_C(0x500008));
    walk_data((UINT64_C(21) << 10) | UINT64_C(0xc7));
    hint(UINT64_C(0x50003f), 2, 1, UINT64_C(0x15000));
    hint(UINT64_C(0x50003f), 3, 1, UINT64_C(0x15000));
    hint(UINT64_C(0x50003f), 1,
         0); // A DTLB entry is not an instruction-side entry.
    hint(UINT64_C(0x800050003f), 2,
         0); // A noncanonical alias must not use the resident VPN.
    privilege = 0;
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    hint(UINT64_C(0x50003f), 2, 0);
    privilege = 1;
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    falling();
    ex_valid = 1;
    eval();
    CHECK(index_valid && !resolve_valid);
    tick_model();
    falling();
    ex_valid = 0;
    address = UINT64_C(0xdeadbeef);
    eval();
    CHECK(result_valid && resolve_valid &&
          resolve_address == UINT64_C(0x15008) && outcome == 1);
    tick_model();
    falling();
    // Current permissions are checked even on a resident supervisor mapping.
    privilege = 0;
    ex_valid = 1;
    address = UINT64_C(0x500008);
    tick_model();
    falling();
    ex_valid = 0;
    eval();
    CHECK(result_valid && outcome == 4 && !resolve_valid);
    tick_model();
    falling();
    privilege = 3;
    mstatus = (UINT64_C(1) << 17) | (UINT64_C(1) << 11);
    ex_valid = 1;
    tick_model();
    falling();
    ex_valid = 0;
    eval();
    CHECK(resolve_valid && resolve_address == UINT64_C(0x15008));
    tick_model();
    falling();
    privilege = 1;
    mstatus = 0;
    // Architectural invalidation forces a fresh walk and permits remapping.
    fence();
    offer_wb(UINT64_C(0x500008));
    walk_data((UINT64_C(22) << 10) | UINT64_C(0xc7));
    ex_valid = 1;
    tick_model();
    falling();
    ex_valid = 0;
    eval();
    CHECK(resolve_valid && resolve_address == UINT64_C(0x16008));
    tick_model();
    falling();
    // Atomic MEM translation checks permissions without exposing a speculative
    // physical cache request. WB carries the exact operation to the service.
    for (int operation = 3; operation <= 9; operation++) {
      access = ((operation)&low_mask(4));
      address = operation >= 6 ? UINT64_C(0x50003f) : UINT64_C(0x500008);
      ex_valid = 1;
      tick_model();
      falling();
      ex_valid = 0;
      eval();
      CHECK(result_valid && outcome == 0 && !resolve_valid);
      tick_model();
      falling();
      wb_valid = 1;
      physical_ready = 1;
      eval();
      CHECK(wb_ready && physical_valid &&
            physical_address ==
                (operation >= 6 ? UINT64_C(0x1603f) : UINT64_C(0x16008)) &&
            physical_access == access);
      tick_model();
      falling();
      wb_valid = 0;
      physical_ready = 0;
      return_data(operation == 4 ? UINT64_C(1) : UINT64_C(0xffffffff81234567),
                  1);
    }
    // Read-only mappings permit LR, but fault SC/AMO with the original VA.
    access = 1;
    fence();
    offer_wb(UINT64_C(0x500008));
    walk_data((UINT64_C(22) << 10) | UINT64_C(0x43));
    hint(
        UINT64_C(0x50003f), 3, 1,
        UINT64_C(
            0x16000)); // Hints require some PTE permission, not store permission or D.
    access = 3;
    wb_valid = 1;
    physical_ready = 1;
    eval();
    CHECK(wb_ready && !wb_fault && physical_access == 3);
    tick_model();
    falling();
    wb_valid = 0;
    physical_ready = 0;
    return_data(UINT64_C(7), 1);
    for (int operation = 4; operation <= 6; operation++) {
      access = ((operation)&low_mask(4));
      address = operation == 6 ? UINT64_C(0x50003f) : UINT64_C(0x500008);
      wb_valid = 1;
      eval();
      CHECK(wb_fault && !wb_ready && !physical_valid &&
            wb_fault_bits.pcause == 15 && wb_fault_bits.pvalue == address);
      tick_model();
      falling();
      wb_valid = 0;
    }
    // Management uses read-or-write PTE permission, does not require D, and
    // still carries store/AMO fault classification when translation fails.
    for (int operation = 7; operation <= 9; operation++) {
      access = ((operation)&low_mask(4));
      address = UINT64_C(0x50003f);
      wb_valid = 1;
      physical_ready = 1;
      eval();
      CHECK(wb_ready && !wb_fault && physical_valid &&
            physical_access == access && physical_address == UINT64_C(0x1603f));
      tick_model();
      falling();
      wb_valid = 0;
      physical_ready = 0;
      return_data(0, 1);
    }
    access = 8;
    fence();
    offer_wb(UINT64_C(0x50003f));
    walk_data(0);
    wb_valid = 1;
    eval();
    CHECK(wb_fault && !wb_ready && !physical_valid &&
          wb_fault_bits.pcause == 15 &&
          wb_fault_bits.pvalue == UINT64_C(0x50003f));
    tick_model();
    falling();
    wb_valid = 0;
    // Device PMAs reject all atomics without a physical or uncached transaction.
    privilege = 3;
    address = UINT64_C(0x2000);
    for (int operation = 3; operation <= 6; operation++) {
      access = ((operation)&low_mask(4));
      ex_valid = 1;
      tick_model();
      falling();
      ex_valid = 0;
      eval();
      CHECK(result_valid && outcome == 5 && !resolve_valid && !physical_valid);
      tick_model();
      falling();
    }
    // Zero uses its own PMA capability, not atomic permission or natural alignment.
    access = 6;
    for (int scenario = 0; scenario < 4; scenario++) {
      address = scenario == 0   ? UINT64_C(0x2403)
                : scenario == 1 ? UINT64_C(0x2103)
                : scenario == 2 ? UINT64_C(0x253f)
                                : UINT64_C(0x233f);
      ex_valid = 1;
      tick_model();
      falling();
      ex_valid = 0;
      eval();
      CHECK(result_valid && !resolve_valid &&
            outcome == (scenario < 2 ? 5 : 0));
      tick_model();
      falling();
    }
    // Maintenance has no zero/atomic capability requirement, but checks the
    // whole block and requires at least one of read/write even on device PMAs.
    for (int operation = 7; operation <= 9; operation++)
      for (int scenario = 0; scenario < 4; scenario++) {
        access = ((operation)&low_mask(4));
        address = scenario == 0   ? UINT64_C(0x2103)
                  : scenario == 1 ? UINT64_C(0x2403)
                  : scenario == 2 ? UINT64_C(0x203f)
                                  : UINT64_C(0x253f);
        ex_valid = 1;
        tick_model();
        falling();
        ex_valid = 0;
        eval();
        CHECK(result_valid && !resolve_valid &&
              outcome == (scenario < 2 ? 5 : 0));
        tick_model();
        falling();
      }
    access = 1;
    privilege = 1;
    address = UINT64_C(0x500008);
    fence();
    offer_wb(UINT64_C(0x500008));
    walk_data((UINT64_C(22) << 10) | UINT64_C(0xc7));
    // A WB request owns the DTLB instead of the younger MEM lookup.
    ex_valid = 1;
    tick_model();
    falling();
    ex_valid = 0;
    wb_valid = 1;
    address = UINT64_C(0x500010);
    eval();
    CHECK(result_valid && outcome == 3 && !resolve_valid &&
          physical_address == UINT64_C(0x16010));
    tick_model();
    falling();
    wb_valid = 0;
    // A pulsed older WB miss is remembered while an instruction walk runs.
    // Dropping the fetch consumer (ordinary replay) must not restart that walk.
    fence();
    fetch_valid = 1;
    wait_request(UINT64_C(0x10000));
    address = UINT64_C(0x500008);
    wb_valid = 1;
    tick_model();
    falling();
    wb_valid = 0;
    fetch_valid = 0;
    reply(UINT64_C(0x10000), (UINT64_C(17) << 10) | 1);
    reply(UINT64_C(0x11010), (UINT64_C(18) << 10) | 1);
    reply(UINT64_C(0x12000), (UINT64_C(20) << 10) | UINT64_C(0xcb));
    // Even a new fetch miss cannot take the next walker slot from retained WB.
    fetch_valid = 1;
    fetch_address = UINT64_C(0x402000);
    walk_data((UINT64_C(21) << 10) | UINT64_C(0xc7));
    fetch_valid = 0;
    fetch_address = UINT64_C(0x400000);
    // An accepted fetch PTE survives cancellation. A normal WB transaction can
    // issue while that old response is outstanding; FIFO ownership distinguishes both.
    fence();
    fetch_valid = 1;
    wait_request(UINT64_C(0x10000));
    physical_ready = 1;
    tick_model();
    falling();
    physical_ready = 0;
    fetch_valid = 0;
    invalidate = 1;
    tick_model();
    falling();
    invalidate = 0;
    privilege = 3;
    address = UINT64_C(0x80);
    wb_valid = 1;
    physical_ready = 1;
    eval();
    CHECK(wb_ready && physical_valid && physical_address == UINT64_C(0x80));
    tick_model();
    falling();
    wb_valid = 0;
    physical_ready = 0;
    privilege = 1;
    fetch_valid = 1;
    fetch_address = UINT64_C(0x400000);
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index) {
      tick_model();
      CHECK(!physical_valid);
    }
    falling();
    return_data((UINT64_C(17) << 10) | 1);
    return_data(UINT64_C(0xdeadbeef), 1);
    // A fresh translation must start at the root, never use the canceled PTE.
    reply(UINT64_C(0x10000), (UINT64_C(17) << 10) | 1);
    reply(UINT64_C(0x11010), (UINT64_C(18) << 10) | 1);
    reply(UINT64_C(0x12000), (UINT64_C(20) << 10) | UINT64_C(0xcb));
    for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
      tick_model();
    falling();
    CHECK(fetch_resolution.pdisposition == 0 &&
          fetch_physical == UINT64_C(0x14000));
    fetch_valid = 0;
    hint(UINT64_C(0x40003f), 1, 1, UINT64_C(0x14000));
    // PTE physical admission failure becomes a precise original data VA fault.
    offer_wb(UINT64_C(0x501038));
    wait_request(UINT64_C(0x10000));
    physical_fault = 1;
    tick_model();
    falling();
    physical_fault = 0;
    for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
      tick_model();
    falling();
    wb_valid = 1;
    eval();
    CHECK(wb_fault && !wb_ready && wb_fault_bits.pcause == 5 &&
          wb_fault_bits.pvalue == UINT64_C(0x501038));
    // An architecturally invalid page table in a device region faults locally;
    // admitting ordinary MMIO must not accidentally admit speculative PTE reads.
    falling();
    wb_valid = 0;
    fence();
    satp = UINT64_C(0x8000000000000002);
    offer_wb(UINT64_C(0x500008));
    for (unsigned repeat_index = 0; repeat_index < (12); ++repeat_index) {
      tick_model();
      CHECK(!physical_valid);
    }
    falling();
    wb_valid = 1;
    eval();
    CHECK(wb_fault && !wb_ready && wb_fault_bits.pcause == 5 &&
          wb_fault_bits.pvalue == UINT64_C(0x500008));
    falling();
    wb_valid = 0;
    privilege = 3;
    satp = 0;
    fence();
    // Byte masks and shifts preserve all neighboring lanes. Prefix acceptance
    // is irrevocable, but no second fragment appears before its response.
    locality = 4;
    start_split(UINT64_C(0x307), 2, 3);
    locality = 0;
    wait_request(UINT64_C(0x300));
    CHECK(physical_locality == 4);
    CHECK(physical_mask == UINT64_C(0x80) &&
          physical_data == UINT64_C(0x1100000000000000));
    physical_ready = 1;
    tick_model();
    falling();
    physical_ready = 0;
    for (unsigned repeat_index = 0; repeat_index < (4); ++repeat_index) {
      tick_model();
      CHECK(!physical_valid && !split_completed);
    }
    falling();
    return_data(0);
    wait_request(UINT64_C(0x308));
    CHECK(physical_locality == 4);
    CHECK(physical_mask == UINT64_C(0x7f) &&
          physical_data == UINT64_C(0x88776655443322));
    physical_ready = 1;
    tick_model();
    falling();
    physical_ready = 0;
    return_data(0);
    finish_split(0);
    start_split(UINT64_C(0x303), 1, 1);
    reply(UINT64_C(0x300), UINT64_C(0x8877665544332211));
    finish_split(UINT64_C(0x5544));
    // A cached first translation and missing second PTE must never reissue
    // the accepted first-page store or require rollback.
    privilege = 1;
    satp = UINT64_C(0x8000000000000010);
    fence();
    locality = 2;
    start_split(UINT64_C(0x500ffd), 2, 3);
    locality = 0;
    walk_data((UINT64_C(21) << 10) | UINT64_C(0xc7));
    wait_request(UINT64_C(0x15ff8));
    CHECK(physical_mask == UINT64_C(0xe0));
    CHECK(physical_locality == 2);
    physical_ready = 1;
    tick_model();
    falling();
    physical_ready = 0;
    return_data(0);
    reply(UINT64_C(0x10000), (UINT64_C(17) << 10) | 1);
    reply(UINT64_C(0x11010), (UINT64_C(18) << 10) | 1);
    reply(UINT64_C(0x12808), 0);
    finish_split(0, UINT64_C(0x501000), 1);
    // Device fragments are rejected locally, even when one natural beat would
    // otherwise cover the access. Atomic alignment remains a core decision.
    privilege = 3;
    start_split(UINT64_C(0x2001), 1, 1);
    finish_split(0, UINT64_C(0x2001), 0, 1);
    // One accepted leaf supplies the full 64 KiB mapping through each bank.
    // Cold walks begin at different subpages; neighboring PTEs are never read.
    privilege = 1;
    satp = UINT64_C(0x8000000000000010);
    access = 1;
    for (int first = 0; first < 16; first += 7) {
      std::uint64_t va;
      va = UINT64_C(0x500000) + ((first * 4096) & low_mask(64)) +
           UINT64_C(0x128);
      fence();
      offer_wb(va);
      walk_at(va, UINT64_C(0x80000000000060cf));
      for (int page = 0; page < 16; page++) {
        data_hit(UINT64_C(0x500000) + ((page * 4096) & low_mask(64)),
                 UINT64_C(0x10000) + ((page * 4096) & low_mask(64)));
        data_hit(UINT64_C(0x500128) + ((page * 4096) & low_mask(64)),
                 UINT64_C(0x10128) + ((page * 4096) & low_mask(64)), 2);
        data_hit(UINT64_C(0x500ff8) + ((page * 4096) & low_mask(64)),
                 UINT64_C(0x10ff8) + ((page * 4096) & low_mask(64)));
      }
      hint(UINT64_C(0x50ffff), 2, 1, UINT64_C(0x1ffc0));
      // Outside the mapping, speculative MEM returns Slow, without walking.
      for (int side = 0; side < 2; side++) {
        address = side == 0 ? UINT64_C(0x4ffff8) : UINT64_C(0x510000);
        ex_valid = 1;
        tick_model();
        falling();
        ex_valid = 0;
        eval();
        CHECK(result_valid && outcome == 0 && !resolve_valid &&
              !physical_valid);
        tick_model();
        falling();
      }
      fetch_address = UINT64_C(0x400128) + ((first * 4096) & low_mask(64));
      fetch_valid = 1;
      walk_at(fetch_address, UINT64_C(0x80000000000060cb));
      for (int page = 0; page < 16; page++) {
        fetch_address = UINT64_C(0x400ff8) + ((page * 4096) & low_mask(64));
        eval();
        CHECK(fetch_resolution.pdisposition == 0 &&
              fetch_physical ==
                  UINT64_C(0x10ff8) + ((page * 4096) & low_mask(64)) &&
              !physical_valid);
        tick_model();
        falling();
      }
      fetch_address = UINT64_C(0x410000);
      eval();
      CHECK(fetch_resolution.pdisposition == 2);
      fetch_valid = 0;
    }
    // Fence removes both banks' whole-region entries, allowing a new base.
    fence();
    fetch_address = UINT64_C(0x400008);
    eval();
    CHECK(fetch_resolution.pdisposition == 2);
    access = 1;
    offer_wb(UINT64_C(0x500008));
    walk_at(UINT64_C(0x500008), UINT64_C(0x80000000000020cf));
    data_hit(UINT64_C(0x500008), 8);
    fetch_valid = 1;
    walk_at(UINT64_C(0x400008), UINT64_C(0x80000000000020cb));
    CHECK(fetch_resolution.pdisposition == 0 && fetch_physical == 8);
    fetch_valid = 0;
    // Warm permission checks must apply to every subpage, including current U mode.
    fence();
    offer_wb(UINT64_C(0x503008));
    walk_at(UINT64_C(0x503008), UINT64_C(0x8000000000006043));
    data_hit(UINT64_C(0x50f008), UINT64_C(0x1f008));
    data_page_fault(UINT64_C(0x50e008), 2);
    privilege = 0;
    data_page_fault(UINT64_C(0x50d008));
    privilege = 1;
    // Missing A, missing D, reserved low PPN, and upper-level N fault, not fill.
    for (int scenario = 0; scenario < 4; scenario++) {
      std::uint64_t leaf;
      fence();
      access = scenario == 1 ? 2 : 1;
      leaf = scenario == 0   ? UINT64_C(0x800000000000608f)
             : scenario == 1 ? UINT64_C(0x800000000000604f)
             : scenario == 2 ? UINT64_C(0x80000000000064cf)
                             : UINT64_C(0x80000000000060cf);
      offer_wb(UINT64_C(0x507128));
      walk_at(UINT64_C(0x507128), leaf, scenario == 3 ? 1 : 0);
      data_page_fault(UINT64_C(0x507128), scenario == 1 ? 2 : 1);
    }
    fence();
    fetch_address = UINT64_C(0x40f008);
    fetch_valid = 1;
    walk_at(fetch_address,
            UINT64_C(0x8000000000006043)); // Readable but not executable.
    CHECK(fetch_resolution.pdisposition == 1 && fetch_resolution.pcause == 12 &&
          fetch_resolution.pvalue == UINT64_C(0x40f008) && !physical_valid);
    fetch_valid = 0;
    // PBMTE is part of translation context. NC/IO stay off the speculative
    // cache path, but retain their selector and exact address through WB.
    for (int kind = 1; kind <= 2; kind++) {
      fence();
      access = 1;
      pbmte = 1;
      offer_wb(UINT64_C(0x500008));
      walk_data((((kind)&low_mask(64)) << 61) | (UINT64_C(21) << 10) |
                UINT64_C(0xc7));
      address = UINT64_C(0x500008);
      ex_valid = 1;
      tick_model();
      falling();
      ex_valid = 0;
      eval();
      CHECK(result_valid && outcome == 0 && !resolve_valid && !physical_valid);
      tick_model();
      falling();
      wb_valid = 1;
      eval();
      CHECK(physical_valid && physical_address == UINT64_C(0x15008) &&
            physical_pbmt == ((kind)&low_mask(2)) && !wb_fault);
      physical_ready = 1;
      tick_model();
      falling();
      wb_valid = 0;
      physical_ready = 0;
      return_data(UINT64_C(0x123456), 1);
      for (int operation = 7; operation <= 9; operation++) {
        access = ((operation)&low_mask(4));
        wb_valid = 1;
        eval();
        CHECK(physical_valid && physical_address == UINT64_C(0x15008) &&
              physical_pbmt == ((kind)&low_mask(2)) && !wb_fault);
        wb_valid = 0;
      }
      access = 1;
      hint(UINT64_C(0x50003f), 2, 0);
      hint(UINT64_C(0x50003f), 3, 0);
      // PBMT must not make unsupported physical atomic service legal.
      access = 3;
      ex_valid = 1;
      tick_model();
      falling();
      ex_valid = 0;
      eval();
      CHECK(result_valid && outcome == 5 && !resolve_valid);
      tick_model();
      falling();
      access = 1;
      // The conservative split path still refuses non-idempotent/noncacheable beats.
      start_split(UINT64_C(0x500007));
      finish_split(0, UINT64_C(0x500007), 0, 1);
      fetch_address = UINT64_C(0x400008);
      fetch_valid = 1;
      walk_at(fetch_address, (((kind)&low_mask(64)) << 61) |
                                 (UINT64_C(20) << 10) | UINT64_C(0xcb));
      CHECK(fetch_resolution.pdisposition == 0 &&
            fetch_physical == UINT64_C(0x14008) &&
            fetch_pbmt == ((kind)&low_mask(2)));
      fetch_valid = 0;
      hint(UINT64_C(0x400008), 1, 0);
      pbmte = 0;
      address = UINT64_C(0x500008);
      ex_valid = 1;
      tick_model();
      falling();
      ex_valid = 0;
      eval();
      CHECK(result_valid && outcome == 0 && !resolve_valid);
      tick_model();
      falling();
      offer_wb(UINT64_C(0x500008));
      walk_data((((kind)&low_mask(64)) << 61) | (UINT64_C(21) << 10) |
                UINT64_C(0xc7));
      data_page_fault(UINT64_C(0x500008));
    }
    // Reserved leaf encodings and PBMT in a nonleaf must fault, not fill.
    pbmte = 1;
    fence();
    offer_wb(UINT64_C(0x500008));
    walk_data((UINT64_C(3) << 61) | (UINT64_C(21) << 10) | UINT64_C(0xc7));
    data_page_fault(UINT64_C(0x500008));
    fence();
    offer_wb(UINT64_C(0x500008));
    reply(UINT64_C(0x10000), (UINT64_C(1) << 61) | (UINT64_C(17) << 10) | 1);
    for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
      tick_model();
    falling();
    data_page_fault(UINT64_C(0x500008));
    // PBMT and Svnapot coexist in a single mapping; permissions remain unchanged.
    fence();
    offer_wb(UINT64_C(0x507008));
    walk_at(UINT64_C(0x507008), UINT64_C(0xa000000000006043));
    address = UINT64_C(0x50f008);
    wb_valid = 1;
    eval();
    CHECK(physical_valid && physical_address == UINT64_C(0x1f008) &&
          physical_pbmt == 1);
    wb_valid = 0;
    data_page_fault(UINT64_C(0x50f008), 2);
    // Bare translation ignores any stale page attributes and returns PMA.
    privilege = 3;
    access = 1;
    address = 8;
    wb_valid = 1;
    eval();
    CHECK(physical_valid && physical_address == 8 && physical_pbmt == 0);
    wb_valid = 0;
    // Guest Bare with G-stage Sv39x4 uses the same TLB/walker, including HLVX X permissions.
    pbmte = 0;
    satp = 0;
    privilege = 1;
    access = 1;
    guest_state = {};
    guest_state.pvirtualized = 1;
    guest_state.phgatp = UINT64_C(0x8000000000000010);
    fence();
    offer_wb(UINT64_C(0x16008));
    reply(UINT64_C(0x10000), (UINT64_C(17) << 10) | 1);
    reply(UINT64_C(0x11000), (UINT64_C(18) << 10) | 1);
    reply(UINT64_C(0x120b0), (UINT64_C(22) << 10) | UINT64_C(0xd7));
    for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
      tick_model();
    falling();
    address = UINT64_C(0x16008);
    wb_valid = 1;
    eval();
    CHECK(physical_valid && physical_address == UINT64_C(0x16008) && !wb_fault);
    wb_valid = 0;
    guest_state.pvirtualized = 0;
    guest_state.phstatus = UINT64_C(0x100);
    guest_access = 2;
    offer_wb(UINT64_C(0x16008));
    // Cached permissions reject HLVX without repeating the successful data walk.
    for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
      tick_model();
    falling();
    wb_valid = 1;
    eval();
    CHECK(wb_fault && wb_fault_bits.pcause == 21 &&
          wb_fault_bits.pvalue == UINT64_C(0x16008) &&
          (wb_fault_bits.pguest.pguest_uvirtual_uaddress &&
           wb_fault_bits.pguest.pguest_uphysical_uaddress == 90120 &&
           wb_fault_bits.pguest.paccess == 0));
    wb_valid = 0;
    // Implicit VS PTE reads themselves undergo G translation and retain their GPA.
    guest_access = 0;
    guest_state.pvirtualized = 1;
    guest_state.pvsatp = UINT64_C(0x8000000000000020);
    fence();
    offer_wb(UINT64_C(0x500008));
    reply(UINT64_C(0x10000), 0);
    for (unsigned repeat_index = 0; repeat_index < (5); ++repeat_index)
      tick_model();
    falling();
    wb_valid = 1;
    eval();
    CHECK(wb_fault && wb_fault_bits.pcause == 21 &&
          wb_fault_bits.pvalue == UINT64_C(0x500008) &&
          (wb_fault_bits.pguest.pguest_uvirtual_uaddress &&
           wb_fault_bits.pguest.pguest_uphysical_uaddress == 131072 &&
           wb_fault_bits.pguest.paccess == 1));
    wb_valid = 0;
    guest_state = {};
    guest_state.phstatus = UINT64_C(0x100);
    guest_access = 2;
    // HLVX also requires executable PMA even with both translation stages Bare.
    fence();
    address = UINT64_C(0x2300);
    wb_valid = 1;
    eval();
    CHECK(wb_fault && !physical_valid && wb_fault_bits.pcause == 5 &&
          wb_fault_bits.pvalue == UINT64_C(0x2300));
    guest_access = 1;
    eval();
    CHECK(physical_valid && !wb_fault);
    wb_valid = 0;
    guest_state = {};
    guest_access = 0;
  });
}