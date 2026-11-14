// Exercises exact fault replay, queued data walks, and SATP invalidation on the host-only MMU.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

constexpr std::uint64_t sv39 = UINT64_C(8) << 60;

void reply_pte(unsigned ppn, std::uint64_t pte, unsigned root_index = 0) {
  eval();
  for (unsigned cycle = 0; cycle < 10 && !physical_valid; ++cycle)
    tick_model();
  CHECK(physical_valid && physical_address == (std::uint64_t(ppn) << 12) + 8 * root_index);
  physical_ready = 1;
  tick_model();
  physical_ready = 0;
  response_valid = 1;
  response_data = pte;
  eval();
  CHECK(response_ready);
  tick_model();
  response_valid = 0;
}

void root_reply(unsigned ppn, std::uint64_t pte) {
  fetch_valid = 1;
  eval();
  CHECK(fetch_resolution.pdisposition == 2); // Replay until the walk completes.
  reply_pte(ppn, pte);
}

void reset_host() {
  reset = 1;
  fetch_valid = wb_valid = invalidate = physical_ready = response_valid = 0;
  prefetch_in = {};
  tick_model();
  reset = 0;
  privilege = 1;
  satp = sv39 | 0x10;
}

void check_instruction_alias(bool outside) {
  reset_host();
  fetch_address = 0x10080;
  // Root leaf maps a 1 GiB superpage. Both destinations have identical low
  // bits and PMA permits a fetch from the low RAM page.
  root_reply(0x10, (outside ? UINT64_C(1) << 46 : 0) | 0x4b);
  tick_model();
  fetch_address += 0x40;
  for (unsigned cycle = 0; cycle < 4; ++cycle) {
    eval();
    CHECK(!physical_valid && fetch_resolution.pdisposition == unsigned(outside));
    if (outside)
      CHECK(fetch_resolution.pcause == 1 && fetch_resolution.pvalue == fetch_address);
    else
      CHECK(fetch_physical == fetch_address);
    tick_model();
  }
  fetch_valid = 0;
  prefetch_in = {1, {fetch_address, 1}};
  tick_model();
  prefetch_in = {};
  tick_model();
  CHECK(physical_prefetch_out.pvalid == !outside && !physical_valid);
  if (!outside)
    CHECK(physical_prefetch_out.pbits.paddress == (fetch_address & ~UINT64_C(63)));
}

void check_data_alias(bool outside) {
  reset_host();
  address = 0x2000;
  access = 1; // Load
  request_width = 2;
  wb_valid = 1;
  tick_model();
  wb_valid = 0;
  // Map the real MMIO region at 0x1_0000_2000 or its bit-48 alias.
  reply_pte(0x10, (UINT64_C(1) << 30) | (outside ? UINT64_C(1) << 46 : 0) | 0x43);
  tick_model();
  wb_valid = 1;
  eval();
  if (!outside) {
    CHECK(!wb_fault && physical_valid && physical_address == UINT64_C(0x100002000));
  } else {
    CHECK(wb_fault && wb_fault_bits.pcause == 5 && wb_fault_bits.pvalue == address && !physical_valid);
    access = 2; // The same resident read-only mapping denies a store first.
    eval();
    CHECK(wb_fault && wb_fault_bits.pcause == 15 && wb_fault_bits.pvalue == address && !physical_valid);
  }
  wb_valid = 0;
}

void check_queued_data_epoch(bool cancel_pending) {
  reset_host();
  fetch_valid = 1;
  fetch_address = 0x10080;
  tick_model();
  fetch_valid = 0;
  constexpr std::uint64_t va = (UINT64_C(1) << 30) + 0x2000;
  address = va;
  access = 1;
  request_width = 2;
  wb_valid = 1;
  tick_model(); // A one-cycle WB miss behind the active instruction walk.
  wb_valid = 0;
  address = 0x4000;
  reply_pte(0x10, 0x4b);
  if (!cancel_pending) {
    reply_pte(0x10, 0, 1); // Captured VA, not the now-idle WB port.
    tick_model();
    wb_valid = 1;
    address = va;
    for (unsigned cycle = 0; cycle < 3; ++cycle) {
      eval();
      CHECK(wb_fault && wb_fault_bits.pcause == 13 && wb_fault_bits.pvalue == va && !physical_valid);
      tick_model();
    }
    access = 2;
    eval();
    CHECK(!wb_fault); // Same VA, but the retained load failure cannot satisfy a store.
    wb_valid = 0;
  }
  invalidate = 1;
  tick_model();
  invalidate = 0;
  satp = sv39 | 0x11;
  for (unsigned cycle = 0; cycle < 8; ++cycle) {
    tick_model();
    CHECK(!physical_valid); // No old pending walk launches against the new root.
  }
  address = va;
  access = 1;
  wb_valid = 1;
  eval();
  CHECK(!wb_fault);
  tick_model();
  wb_valid = 0;
  reply_pte(0x11, (UINT64_C(1) << 30) | 0x43, 1);
  tick_model();
  wb_valid = 1;
  eval();
  CHECK(!wb_fault && physical_valid && physical_address == UINT64_C(0x100002000));
  wb_valid = 0;
}

int main() {
  return run_test([] {
    for (bool invalidate_on_completion : {false, true}) {
      reset = 1;
      fetch_valid = 0;
      tick_model();
      reset = 0;
      privilege = 1;
      fetch_address = 0x10080;
      satp = sv39 | 0x10;
      root_reply(0x10, 0); // Invalid root PTE.
      if (!invalidate_on_completion) {
        for (unsigned i = 0; i < 3; ++i) {
          tick_model();
          CHECK(fetch_resolution.pdisposition == 1 && fetch_resolution.pcause == 12 &&
                fetch_resolution.pvalue == fetch_address && !physical_valid);
        }
      }
      satp = sv39 | 0x11;
      invalidate = 1;
      tick_model();
      invalidate = 0;
      root_reply(0x11, 0x4b); // Supervisor RX superpage, accessed, PPN 0.
      tick_model();
      CHECK(fetch_resolution.pdisposition == 0 && fetch_physical == fetch_address &&
            !physical_valid);
    }
    for (bool outside : {false, true}) {
      check_instruction_alias(outside);
      check_data_alias(outside);
    }
    for (bool cancel_pending : {false, true})
      check_queued_data_epoch(cancel_pending);
  });
}
