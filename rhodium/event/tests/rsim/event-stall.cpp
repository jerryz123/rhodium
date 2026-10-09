// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

extern "C" void event_stall_bind();
extern "C" void event_stall_sample(unsigned, unsigned, unsigned, unsigned,
                                   unsigned, unsigned, unsigned, unsigned);
extern "C" void event_stall_check();
extern "C" void event_stall_finish();

int main() {
  return run_test([] {
    event_stall_bind();
    auto source = std::array{&sources_0_in, &sources_1_in, &sources_2_in};
    auto source_ready =
        std::array{&sources_0_out, &sources_1_out, &sources_2_out};
    auto sink = std::array{&sinks_0_out, &sinks_1_out, &sinks_2_out};
    auto ready = std::array{&sinks_0_in, &sinks_1_in, &sinks_2_in};
    auto gold_source =
        std::array{&gold_sources_0_in, &gold_sources_1_in, &gold_sources_2_in};
    auto gold_ready = std::array{&gold_sources_0_out, &gold_sources_1_out,
                                 &gold_sources_2_out};
    auto gold_sink =
        std::array{&gold_sinks_0_out, &gold_sinks_1_out, &gold_sinks_2_out};
    auto gold_sink_ready =
        std::array{&gold_sinks_0_in, &gold_sinks_1_in, &gold_sinks_2_in};
    for (unsigned step = 0; step < 160; ++step) {
      reset = step == 0 || step == 19 || step == 20 || step == 77;
      for (unsigned lane = 0; lane < 3; ++lane) {
        // Repeat payloads and change or withdraw unaccepted offers.
        source[lane]->pvalid = step < 140 && (step + lane) % 5 != 0;
        source[lane]->pbits = (step + lane) % 4;
        ready[lane]->pready =
            step >= 140 || (step >= 25 && (step + lane) % 7 >= 3);
        *gold_source[lane] = *source[lane];
        *gold_sink_ready[lane] = *ready[lane];
      }
      eval();
      for (unsigned lane = 0; lane < 3; ++lane) {
        CHECK(source_ready[lane]->pready == gold_ready[lane]->pready);
        CHECK(sink[lane]->pvalid == gold_sink[lane]->pvalid);
        if (sink[lane]->pvalid)
          CHECK(sink[lane]->pbits == gold_sink[lane]->pbits);
        event_stall_sample(lane, reset, source[lane]->pvalid,
                           source_ready[lane]->pready, source[lane]->pbits,
                           sink[lane]->pvalid, ready[lane]->pready,
                           sink[lane]->pbits);
      }
      tick_model();
      event_stall_check();
    }
    event_stall_finish();
  });
}
