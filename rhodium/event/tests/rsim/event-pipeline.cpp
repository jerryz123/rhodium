// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "events.h"

extern "C" void event_pipeline_sample(unsigned rst, unsigned clear,
                                      unsigned valid, unsigned payload,
                                      unsigned out_valid, unsigned out_payload);
extern "C" void event_pipeline_check();
extern "C" void event_pipeline_finish();

int main() {
  return run_test([] {
    rheg::graph().bind_manifest(rheg_generated::manifest());
    for (unsigned step = 0; step < 80; step++) {
      reset = (step == 0 || step == 8 || step == 9 || step == 34);
      flush = (step == 5 || step == 17 || step == 18 || step == 33 ||
               step == 34 || step == 46 || step == 73);
      source_in.pvalid = step < 72 && (step % 7 != 2) && (step % 7 != 3);
      source_in.pbits = (std::uint32_t((step * 13) % 17) & 255u);

      eval();
      // Sample the transfer edge before sequential outputs advance; compare
      // the graph after every DPI effect on that edge has completed.
      event_pipeline_sample(
          std::uint32_t(reset), std::uint32_t(flush),
          std::uint32_t(source_in.pvalid), std::uint32_t(source_in.pbits),
          std::uint32_t(sink_out.pvalid), std::uint32_t(sink_out.pbits));
      tick_model();
      event_pipeline_check();
    }
    event_pipeline_finish();
  });
}
