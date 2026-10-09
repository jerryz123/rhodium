// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "events.h"

extern "C" void event_queue_sample(unsigned, unsigned, unsigned, unsigned,
                                   unsigned, unsigned, unsigned, unsigned);
extern "C" void event_queue_check();
extern "C" void event_queue_finish();

int main() {
  return run_test([] {
    rheg::graph().bind_manifest(rheg_generated::manifest());
    auto source =
        std::array{&sources_0_in, &sources_1_in, &sources_2_in, &sources_3_in,
                   &sources_4_in, &sources_5_in, &sources_6_in, &sources_7_in,
                   &sources_8_in, &sources_9_in};
    auto ready = std::array{&sources_0_out, &sources_1_out, &sources_2_out,
                            &sources_3_out, &sources_4_out, &sources_5_out,
                            &sources_6_out, &sources_7_out, &sources_8_out,
                            &sources_9_out};
    auto sink = std::array{
        &sinks_0_out, &sinks_1_out, &sinks_2_out, &sinks_3_out, &sinks_4_out,
        &sinks_5_out, &sinks_6_out, &sinks_7_out, &sinks_8_out, &sinks_9_out};
    auto sink_ready = std::array{
        &sinks_0_in, &sinks_1_in, &sinks_2_in, &sinks_3_in, &sinks_4_in,
        &sinks_5_in, &sinks_6_in, &sinks_7_in, &sinks_8_in, &sinks_9_in};
    auto gold_source =
        std::array{&gold_sources_0_in, &gold_sources_1_in, &gold_sources_2_in,
                   &gold_sources_3_in, &gold_sources_4_in, &gold_sources_5_in,
                   &gold_sources_6_in, &gold_sources_7_in, &gold_sources_8_in,
                   &gold_sources_9_in};
    auto gold_ready = std::array{&gold_sources_0_out, &gold_sources_1_out,
                                 &gold_sources_2_out, &gold_sources_3_out,
                                 &gold_sources_4_out, &gold_sources_5_out,
                                 &gold_sources_6_out, &gold_sources_7_out,
                                 &gold_sources_8_out, &gold_sources_9_out};
    auto gold_sink =
        std::array{&gold_sinks_0_out, &gold_sinks_1_out, &gold_sinks_2_out,
                   &gold_sinks_3_out, &gold_sinks_4_out, &gold_sinks_5_out,
                   &gold_sinks_6_out, &gold_sinks_7_out, &gold_sinks_8_out,
                   &gold_sinks_9_out};
    auto gold_sink_ready = std::array{
        &gold_sinks_0_in, &gold_sinks_1_in, &gold_sinks_2_in, &gold_sinks_3_in,
        &gold_sinks_4_in, &gold_sinks_5_in, &gold_sinks_6_in, &gold_sinks_7_in,
        &gold_sinks_8_in, &gold_sinks_9_in};
    std::array<bool, 10> accepted;
    accepted.fill(true);
    std::uint32_t random_state = 0xca11ab1e;
    auto random_word = [&] {
      random_state = random_state * 1664525u + 1013904223u;
      return random_state ^ (random_state >> 16);
    };
    for (unsigned step = 0; step < 600; ++step) {
      reset = step == 0 || step == 40 || step == 121 || step == 122;
      for (unsigned i = 0; i < 10; ++i) {
        auto &input = *source[i];
        if (accepted[i] || !input.pvalid || reset) {
          input.pvalid =
              step < 500 &&
              (step < 20 ? step % 2 == 1
                         : (step < 90 || (step >= 350 && step < 410) ||
                            random_word() % 5 != 0));
          input.pbits = ((random_word() >> 24) & 255) % 8;
        }
        sink_ready[i]->pready =
            step >= 500 ||
            (step < 20 || (step >= 41 && step < 90) ||
             (step >= 371 && step < 410) || (random_word() % (i % 3 + 2) == 0));
        if ((step >= 21 && step <= 40) || (step >= 101 && step <= 121) ||
            (step >= 350 && step <= 370))
          sink_ready[i]->pready = false;
        *gold_source[i] = input;
        *gold_sink_ready[i] = *sink_ready[i];
      }
      eval();
      for (unsigned i = 0; i < 10; ++i) {
        CHECK(ready[i]->pready == gold_ready[i]->pready);
        CHECK(sink[i]->pvalid == gold_sink[i]->pvalid);
        if (sink[i]->pvalid)
          CHECK(sink[i]->pbits == gold_sink[i]->pbits);
        accepted[i] = source[i]->pvalid && ready[i]->pready;
        event_queue_sample(i, reset, source[i]->pvalid, ready[i]->pready,
                           source[i]->pbits, sink[i]->pvalid,
                           sink_ready[i]->pready, sink[i]->pbits);
      }
      tick_model();
      event_queue_check();
    }
    event_queue_finish();
  });
}
