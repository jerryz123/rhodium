// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "events.h"

extern "C" void event_window_sample(unsigned, unsigned, unsigned, unsigned,
                                    unsigned, unsigned, unsigned, unsigned,
                                    unsigned, unsigned, unsigned, unsigned,
                                    unsigned, unsigned);
extern "C" void event_window_check();
extern "C" void event_window_finish();

int main() {
  return run_test([] {
    rheg::graph().bind_manifest(rheg_generated::manifest());
    eval();
    for (unsigned step = 0; step < 160; ++step) {
      reset = step == 0 || step == 79;
      flush = step == 19 || step == 20 || step == 55 || step == 111;
      // Occupancy comes from the previous settled edge, as in the SV driver.
      remove_count = std::min<unsigned>(step % 4, occupancy);
      if (step % 8 < 4)
        remove_count = 0;
      if (step % 16 == 12)
        remove_count = occupancy;
      selected = occupancy == 0 ? 0 : occupancy > 1 && step % 3 == 0 ? 3 : 1;
      capture = step % 4 != 1 && int(occupancy) - int(remove_count) < 3;
      append_count =
          capture && step % 3 == 0 && int(occupancy) - int(remove_count) < 2
              ? 2
              : 1;
      source_in.pvalid = step < 150 && (capture || step % 3 == 0);
      live = source_in.pvalid && step % 3 != 1;
      emit = (selected != 0 || live) && step % 5 != 0;
      source_in.pbits = step % 3;
      sink_in.pready = step % 7 > 2;
      eval();
      event_window_sample(reset, flush, emit, remove_count, selected, live,
                          capture, append_count, source_in.pvalid,
                          source_in.pbits, sink_in.pready, sink_out.pvalid,
                          sink_out.pbits, occupancy);
      tick_model();
      event_window_check();
    }
    event_window_finish();
  });
}
