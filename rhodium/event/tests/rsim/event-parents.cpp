// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

extern "C" void parents_bind();
extern "C" void parents_sample(unsigned, unsigned, unsigned, unsigned, unsigned,
                               unsigned, unsigned, unsigned);
extern "C" void parents_check();
extern "C" void parents_finish();

int main() {
  return run_test([] {
    parents_bind();
    auto tick = [] {
      eval();
      parents_sample(reset, flush, source_in.pvalid, source_in.pbits,
                     wb_out.pvalid, wb_out.pbits, cache_out.pvalid,
                     both_out.pvalid);
      if (!reset) {
        CHECK(cache_out.pbits == wb_out.pbits);
        CHECK(both_out.pbits == wb_out.pbits);
      }
      tick_model();
      parents_check();
    };
    reset = true;
    flush = false;
    force_cache_event = false;
    source_in = {};
    tick();
    reset = false;
    std::uint32_t rng = 0xc7651234;
    for (unsigned i = 0; i < 1000; ++i) {
      rng ^= rng << 13;
      rng ^= rng >> 17;
      rng ^= rng << 5;
      reset = i % 223 == 222;
      flush = ((rng >> 2) & 7) == 0;
      source_in.pvalid = rng & 1;
      source_in.pbits = (rng & 2) ? 0x55 : 0x54;
      tick();
    }
    reset = false;
    flush = false;
    source_in.pvalid = false;
    tick();
    tick();
    parents_finish();

    // Force a child observation while its selected intermediate parent is absent.
    dut = Model{};
    reset = true;
    flush = false;
    force_cache_event = true;
    source_in = {};
    tick_model();
    tick_model();
    reset = false;
    source_in.pvalid = true;
    source_in.pbits = 0x54;
    expect_failure("__event_parent_present_", [] {
      for (unsigned i = 0; i < 3; ++i)
        tick_model();
    });
  });
}
