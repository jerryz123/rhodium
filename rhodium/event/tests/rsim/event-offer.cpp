// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

extern "C" void event_offer_bind();
extern "C" void event_offer_sample(unsigned, unsigned, unsigned, unsigned,
                                   unsigned);
extern "C" void event_offer_check();
extern "C" void event_offer_finish();

int main() {
  return run_test([] {
    event_offer_bind();
    for (unsigned step = 0; step < 100; ++step) {
      reset = step == 0 || step == 39;
      source_in.pvalid = step % 10 != 5;
      source_in.pbits = (step / 2) % 4;
      sink_in.pready = step % 4 >= 2;
      observe = step % 7 >= 2;
      // Each decade begins with an identical-payload rejected/replayed pair.
      if (step % 10 < 2) {
        source_in.pvalid = true;
        source_in.pbits = 0x2a;
        observe = true;
        sink_in.pready = step % 10 == 1;
      }
      eval();
      // Check invalid cycles too: observation must not change functional wiring.
      CHECK(sink_out.pvalid == source_in.pvalid &&
            sink_out.pbits == source_in.pbits);
      CHECK(offer_ready == sink_in.pready);
      event_offer_sample(reset, source_in.pvalid, sink_in.pready, observe,
                         source_in.pbits);
      tick_model();
      event_offer_check();
    }
    event_offer_finish();
  });
}
