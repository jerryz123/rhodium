// Checks the shared committed-store FIFO, physical hazards, bounded age, and
// full replacement.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
using store_t = std::remove_cvref_t<decltype(enqueue_in.pbits)>;

void enqueue(store_t store) {
  enqueue_in = {.pvalid = 1, .pbits = store};
  eval();
  CHECK(enqueue_out.pready);
  tick_model();
  enqueue_in = {};
}
void run_case() {
  reset = 1;
  tick_model();
  reset = 0;
  CHECK(empty && !full && enqueue_out.pready && count == 0 &&
        !drain_out.pvalid);
  enqueue_in = {.pvalid = 1,
                .pbits = {.paddress = UINT64_C(4096),
                          .pway = 0,
                          .pdata = UINT64_C(4386),
                          .pmask = UINT64_C(3),
                          .pmark_udirty = 1}};
  query_address = UINT64_C(4097);
  query_mask = UINT64_C(2);
  probe_address = UINT64_C(4152);
  eval();
  CHECK(word_hazard && line_hazard && !queued_word_hazard &&
        !queued_line_hazard);
  tick_model();
  enqueue_in = {};
  enqueue({.paddress = UINT64_C(8192),
           .pway = 1,
           .pdata = UINT64_C(13124),
           .pmask = UINT64_C(12),
           .pmark_udirty = 0});
  enqueue({.paddress = UINT64_C(12288),
           .pway = 0,
           .pdata = UINT64_C(21862),
           .pmask = UINT64_C(48),
           .pmark_udirty = 0});
  enqueue({.paddress = UINT64_C(16384),
           .pway = 1,
           .pdata = UINT64_C(30600),
           .pmask = UINT64_C(192),
           .pmark_udirty = 0});
  CHECK(count == 4 && full && !enqueue_out.pready && !empty &&
        drain_out.pbits.paddress == UINT64_C(4096));
  for (int entry = 0; entry < 4; entry++) {
    query_address = UINT64_C(4096) + (UINT64_C(4096) * entry);
    query_mask = entry == 0   ? UINT64_C(3)
                 : entry == 1 ? UINT64_C(12)
                 : entry == 2 ? UINT64_C(48)
                              : UINT64_C(192);
    probe_address = query_address + UINT64_C(56);
    eval();
    CHECK(word_hazard && queued_word_hazard && line_hazard &&
          queued_line_hazard);
    query_address += 8;
    query_mask = UINT64_C(255);
    eval();
    CHECK(!word_hazard);
    query_address += UINT64_C(1048576);
    probe_address += UINT64_C(1048576);
    eval();
    CHECK(!word_hazard && !line_hazard);
  }
  for (unsigned repeat_index = 0; repeat_index < (10); ++repeat_index)
    tick_model();
  CHECK(urgent && drain_out.pbits.pdata == UINT64_C(4386) &&
        drain_out.pbits.pmark_udirty);

  enqueue_in = {.pvalid = 1,
                .pbits = {.paddress = UINT64_C(20480),
                          .pway = 0,
                          .pdata = UINT64_C(39338),
                          .pmask = UINT64_C(255),
                          .pmark_udirty = 0}};
  drain_in.pready = 1;
  full_drain = 1;
  eval();
  CHECK(enqueue_out.pready);
  tick_model();
  enqueue_in = {};
  drain_in.pready = 0;
  full_drain = 0;
  CHECK(count == 4 && full && drain_out.pbits.paddress == UINT64_C(8192) &&
        !urgent);

  drain_in.pready = 1;
  CHECK(drain_out.pbits.paddress == UINT64_C(8192));
  tick_model();
  CHECK(drain_out.pbits.paddress == UINT64_C(12288));
  tick_model();
  CHECK(drain_out.pbits.paddress == UINT64_C(16384));
  tick_model();
  CHECK(drain_out.pbits.paddress == UINT64_C(20480));
  tick_model();
  CHECK(empty && !full && count == 0 && !drain_out.pvalid);
}

int main() { return run_test(run_case); }
