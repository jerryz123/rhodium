// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

extern "C" void offer_register_bind();
extern "C" void offer_register_sample(unsigned, unsigned, unsigned, unsigned,
                                      unsigned, unsigned);
extern "C" void offer_register_check();
extern "C" void offer_register_finish();

int main() {
  return run_test([] {
    offer_register_bind();
    auto tick = [] {
      eval();
      offer_register_sample(reset, source_in.pvalid, source_in.pbits,
                            sink_out.pvalid, sink_in.pready, sink_out.pbits);
      tick_model();
      offer_register_check();
    };
    reset = true;
    source_in = {};
    sink_in = {};
    tick();
    reset = false;
    source_in.pvalid = true;
    source_in.pbits = 0x55;
    tick();
    source_in.pbits = 0xaa;
    tick(); // Replace a stalled owner.
    sink_in.pready = true;
    tick(); // Deliver the old owner while capturing a new one.
    source_in.pvalid = false;
    tick(); // Drain without replacement.
    sink_in.pready = false;
    source_in.pvalid = true;
    tick();
    reset = true;
    tick(); // Reset a pending owner.
    reset = false;
    std::uint32_t rng = 0xc7651234;
    for (unsigned i = 0; i < 1000; ++i) {
      rng ^= rng << 13;
      rng ^= rng >> 17;
      rng ^= rng << 5;
      reset = (rng & 255) == 0;
      source_in.pvalid = rng & 1;
      source_in.pbits = (rng & 2) ? 0x55 : 0xaa;
      sink_in.pready = (rng >> 2) & 1;
      tick();
    }
    reset = false;
    source_in.pvalid = false;
    sink_in.pready = true;
    tick();
    tick();
    offer_register_finish();
  });
}
