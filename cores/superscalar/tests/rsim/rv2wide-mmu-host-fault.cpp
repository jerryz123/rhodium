// Exercises instruction fault replay and SATP invalidation on the host-only MMU.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

constexpr std::uint64_t sv39 = UINT64_C(8) << 60;

void root_reply(unsigned ppn, std::uint64_t pte) {
  fetch_valid = 1;
  eval();
  CHECK(fetch_resolution.pdisposition == 2); // Replay until the walk completes.
  for (unsigned cycle = 0; cycle < 10 && !physical_valid; ++cycle)
    tick_model();
  CHECK(physical_valid && physical_address == (std::uint64_t(ppn) << 12));
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
  });
}
