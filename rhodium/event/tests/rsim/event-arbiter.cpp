// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "events.h"

extern "C" void event_arbiter_sample(unsigned lane, unsigned rst,
                                     unsigned valid_mask, unsigned ready_mask,
                                     unsigned payloads, unsigned take_mask,
                                     unsigned join_fire, unsigned out_valid,
                                     unsigned out_ready, unsigned out_payload);
extern "C" void event_arbiter_check();
extern "C" void event_arbiter_finish();

int main() {
  return run_test([] {
    rheg::graph().bind_manifest(rheg_generated::manifest());
    auto source = std::array{&sources_0_in,  &sources_1_in,  &sources_2_in,
                             &sources_3_in,  &sources_4_in,  &sources_5_in,
                             &sources_6_in,  &sources_7_in,  &sources_8_in,
                             &sources_9_in,  &sources_10_in, &sources_11_in,
                             &sources_12_in, &sources_13_in, &sources_14_in};
    auto source_response = std::array{
        &sources_0_out,  &sources_1_out,  &sources_2_out,  &sources_3_out,
        &sources_4_out,  &sources_5_out,  &sources_6_out,  &sources_7_out,
        &sources_8_out,  &sources_9_out,  &sources_10_out, &sources_11_out,
        &sources_12_out, &sources_13_out, &sources_14_out};
    auto gold_source_response = std::array{
        &gold_sources_0_out,  &gold_sources_1_out,  &gold_sources_2_out,
        &gold_sources_3_out,  &gold_sources_4_out,  &gold_sources_5_out,
        &gold_sources_6_out,  &gold_sources_7_out,  &gold_sources_8_out,
        &gold_sources_9_out,  &gold_sources_10_out, &gold_sources_11_out,
        &gold_sources_12_out, &gold_sources_13_out, &gold_sources_14_out};
    auto sink = std::array{&sinks_0_out, &sinks_1_out, &sinks_2_out,
                           &sinks_3_out, &sinks_4_out};
    auto sink_ready = std::array{&sinks_0_in, &sinks_1_in, &sinks_2_in,
                                 &sinks_3_in, &sinks_4_in};
    auto gold_sink =
        std::array{&gold_sinks_0_out, &gold_sinks_1_out, &gold_sinks_2_out,
                   &gold_sinks_3_out, &gold_sinks_4_out};
    auto joined = std::array{&join0, &join1, &join2, &join3, &join4};
    auto gold_source = std::array{
        &gold_sources_0_in,  &gold_sources_1_in,  &gold_sources_2_in,
        &gold_sources_3_in,  &gold_sources_4_in,  &gold_sources_5_in,
        &gold_sources_6_in,  &gold_sources_7_in,  &gold_sources_8_in,
        &gold_sources_9_in,  &gold_sources_10_in, &gold_sources_11_in,
        &gold_sources_12_in, &gold_sources_13_in, &gold_sources_14_in};
    auto gold_sink_ready =
        std::array{&gold_sinks_0_in, &gold_sinks_1_in, &gold_sinks_2_in,
                   &gold_sinks_3_in, &gold_sinks_4_in};
    std::array<bool, 15> accepted{};
    std::uint32_t random_state = 0xcedab123u;
    auto random_word = [&] {
      random_state = random_state * 1664525u + 1013904223u;
      return random_state ^ (random_state >> 16);
    };
    for (unsigned i = 0; i < source.size(); ++i) {
      (*source[i]) = {};
      accepted[i] = 1;
    }
    for (unsigned step = 0; step < 450; step++) {
      reset = step == 0 || step == 100 || step == 151 || step == 152;
      for (unsigned i = 0; i < source.size(); ++i) {
        if (accepted[i] || !source[i]->pvalid || reset) {
          source[i]->pvalid =
              step < 350 &&
              (step < 8
                   ? (i % 3 == 2 || (i % 3 == 0 && step >= 3))
                   : (step >= 240 && step < 300 ? i % 3 == (step - 240) / 20
                                                : (step >= 20 && step < 46) ||
                                                      random_word() % 3 != 0));
          source[i]->pbits =
              step < 8 ? (std::uint32_t(i % 3 + 1) & 255u)
                       : (std::uint32_t(random_word() >> 24) & 255u) % 4;
        }
      }
      for (unsigned i = 0; i < sink_ready.size(); ++i) {
        sink_ready[i]->pready =
            step >= 350 ||
            (step >= 8 && !(step >= 70 && step <= 100) &&
             ((step >= 20 && step < 46) || (step >= 240 && step < 300) ||
              random_word() % (i % 3 + 2) == 0));
      }
      for (unsigned i = 0; i < source.size(); ++i)
        *gold_source[i] = *source[i];
      for (unsigned i = 0; i < sink_ready.size(); ++i)
        *gold_sink_ready[i] = *sink_ready[i];
      eval();
      auto taken =
          std::array{std::uint32_t(take0_0 | (take0_1 << 1) | (take0_2 << 2)),
                     std::uint32_t(take1_0 | (take1_1 << 1) | (take1_2 << 2)),
                     std::uint32_t(take2_0 | (take2_1 << 1) | (take2_2 << 2)),
                     std::uint32_t(take3_0 | (take3_1 << 1) | (take3_2 << 2)),
                     std::uint32_t(take4_0 | (take4_1 << 1) | (take4_2 << 2))};
      for (unsigned i = 0; i < source.size(); ++i) {
        CHECK(source_response[i]->pready == gold_source_response[i]->pready);
        accepted[i] = source[i]->pvalid && source_response[i]->pready;
      }
      for (unsigned i = 0; i < sink.size(); ++i) {
        std::uint32_t valid_mask, ready_mask, payloads;
        valid_mask = 0;
        ready_mask = 0;
        payloads = 0;
        for (unsigned j = 0; j < 3; j++) {
          valid_mask |= std::uint32_t(source[i * 3 + j]->pvalid) << j;
          ready_mask |= std::uint32_t(source_response[i * 3 + j]->pready) << j;
          payloads |= std::uint32_t(source[i * 3 + j]->pbits) << (8 * j);
        }
        CHECK(sink[i]->pvalid == gold_sink[i]->pvalid);
        if (sink[i]->pvalid)
          CHECK(sink[i]->pbits == gold_sink[i]->pbits);
        event_arbiter_sample(std::uint32_t(i), std::uint32_t(reset), valid_mask,
                             ready_mask, payloads, std::uint32_t(taken[i]),
                             std::uint32_t((*joined[i])),
                             std::uint32_t(sink[i]->pvalid),
                             std::uint32_t(sink_ready[i]->pready),
                             std::uint32_t(sink[i]->pbits));
      }
      tick_model();
      event_arbiter_check();
    }
    event_arbiter_finish();
  });
}
