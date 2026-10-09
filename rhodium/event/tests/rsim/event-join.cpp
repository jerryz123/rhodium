// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "events.h"

extern "C" void
event_join_sample(unsigned lane, unsigned rst, unsigned in_valid,
                  unsigned in_ready, unsigned payloads, unsigned first_transfer,
                  unsigned replica_transfers, unsigned rejoin_transfer,
                  unsigned combine_transfers, unsigned combined_transfer,
                  unsigned route_transfers, unsigned out_valid,
                  unsigned out_ready, unsigned out_payloads);
extern "C" void event_join_check();
extern "C" void event_join_occurrences(unsigned rst, unsigned input_fire,
                                       unsigned payload, unsigned output_fire,
                                       unsigned result);
extern "C" void event_join_finish();
extern "C" void event_join_bind();

int main() {
  return run_test([] {
    auto source = std::array{&sources_0_in,      &sources_1_in,  &sources_2_in,
                             &sources_3_in,      &sources_4_in,  &sources_5_in,
                             &sources_6_in,      &sources_7_in,  &sources_8_in,
                             &sources_9_in,      &sources_10_in, &sources_11_in,
                             &sequence_source_in};
    auto source_response = std::array{
        &sources_0_out,      &sources_1_out, &sources_2_out,  &sources_3_out,
        &sources_4_out,      &sources_5_out, &sources_6_out,  &sources_7_out,
        &sources_8_out,      &sources_9_out, &sources_10_out, &sources_11_out,
        &sequence_source_out};
    auto gold_source_response = std::array{
        &gold_sources_0_out,      &gold_sources_1_out,  &gold_sources_2_out,
        &gold_sources_3_out,      &gold_sources_4_out,  &gold_sources_5_out,
        &gold_sources_6_out,      &gold_sources_7_out,  &gold_sources_8_out,
        &gold_sources_9_out,      &gold_sources_10_out, &gold_sources_11_out,
        &gold_sequence_source_out};
    auto sink = std::array{&sinks_0_out, &sinks_1_out, &sinks_2_out,
                           &sinks_3_out, &sinks_4_out, &sinks_5_out,
                           &sinks_6_out, &sinks_7_out, &sequence_sink_out};
    auto sink_ready = std::array{&sinks_0_in, &sinks_1_in, &sinks_2_in,
                                 &sinks_3_in, &sinks_4_in, &sinks_5_in,
                                 &sinks_6_in, &sinks_7_in, &sequence_sink_in};
    auto gold_sink = std::array{
        &gold_sinks_0_out, &gold_sinks_1_out, &gold_sinks_2_out,
        &gold_sinks_3_out, &gold_sinks_4_out, &gold_sinks_5_out,
        &gold_sinks_6_out, &gold_sinks_7_out, &gold_sequence_sink_out};
    auto gold_source = std::array{
        &gold_sources_0_in,      &gold_sources_1_in,  &gold_sources_2_in,
        &gold_sources_3_in,      &gold_sources_4_in,  &gold_sources_5_in,
        &gold_sources_6_in,      &gold_sources_7_in,  &gold_sources_8_in,
        &gold_sources_9_in,      &gold_sources_10_in, &gold_sources_11_in,
        &gold_sequence_source_in};
    auto gold_sink_ready =
        std::array{&gold_sinks_0_in, &gold_sinks_1_in, &gold_sinks_2_in,
                   &gold_sinks_3_in, &gold_sinks_4_in, &gold_sinks_5_in,
                   &gold_sinks_6_in, &gold_sinks_7_in, &gold_sequence_sink_in};
    std::array<bool, 13> accepted{};
    std::array<unsigned, 13> taken{};
    std::uint32_t random_state = 0xd0cc2211u;
    auto random_word = [&] {
      random_state = random_state * 1664525u + 1013904223u;
      return random_state ^ (random_state >> 16);
    };
    event_join_bind();
    for (unsigned i = 0; i < source.size(); ++i) {
      (*source[i]) = {};
      accepted[i] = 1;
      taken[i] = 0;
    }
    for (unsigned step = 0; step < 900; step++) {
      reset = step == 0 || step == 100 || step == 301 || step == 302;
      for (unsigned i = 0; i < source.size(); ++i) {
        if (reset)
          taken[i] = 0;
        if (accepted[i] || !source[i]->pvalid || reset) {
          source[i]->pvalid =
              taken[i] < 60 && (step >= 750 || random_word() % 4 != 0);
          source[i]->pbits = (std::uint32_t(random_word() % 8) & 255u);
          if (i == 12)
            source[i]->pbits = (source[i]->pbits & ~1u) | (taken[i] & 1);
        }
      }
      for (unsigned i = 0; i < sink_ready.size(); ++i)
        sink_ready[i]->pready =
            step >= 750 ||
            (!(step >= 70 && step <= 100) && random_word() % (i % 2 + 2) == 0);
      for (unsigned i = 0; i < source.size(); ++i)
        *gold_source[i] = *source[i];
      for (unsigned i = 0; i < sink_ready.size(); ++i)
        *gold_sink_ready[i] = *sink_ready[i];
      eval();
      for (unsigned i = 0; i < source.size(); ++i) {
        CHECK(source_response[i]->pready == gold_source_response[i]->pready);
        accepted[i] = source[i]->pvalid && source_response[i]->pready;
        if (accepted[i] && !reset)
          taken[i]++;
      }
      for (unsigned lane = 0; lane < 4; lane++) {
        std::uint32_t vi, ri, pi, vo, ro, po;
        vi = 0;
        ri = 0;
        pi = 0;
        vo = 0;
        ro = 0;
        po = 0;
        for (unsigned j = 0; j < 3; j++) {
          vi |= std::uint32_t(source[3 * lane + j]->pvalid) << j;
          ri |= std::uint32_t(source_response[3 * lane + j]->pready) << j;
          pi |= std::uint32_t(source[3 * lane + j]->pbits) << (8 * j);
        }
        for (unsigned j = 0; j < 2; j++) {
          CHECK(sink[2 * lane + j]->pvalid == gold_sink[2 * lane + j]->pvalid);
          if (sink[2 * lane + j]->pvalid)
            CHECK(sink[2 * lane + j]->pbits == gold_sink[2 * lane + j]->pbits);
          vo |= std::uint32_t(sink[2 * lane + j]->pvalid) << j;
          ro |= std::uint32_t(sink_ready[2 * lane + j]->pready) << j;
          po |= std::uint32_t(sink[2 * lane + j]->pbits) << (8 * j);
        }
        event_join_sample(std::uint32_t(lane), std::uint32_t(reset), vi, ri, pi,
                          std::uint32_t(((first_fire >> lane) & 1u)),
                          (std::uint32_t(replicas_fire) >> (2 * lane)) & 3,
                          std::uint32_t(((rejoin_fire >> lane) & 1u)),
                          (std::uint32_t(combine_inputs) >> (2 * lane)) & 3,
                          std::uint32_t(((combine_fire >> lane) & 1u)),
                          (std::uint32_t(routes_fire) >> (2 * lane)) & 3, vo,
                          ro, po);
      }
      CHECK(sink[8]->pvalid == gold_sink[8]->pvalid);
      if (sink[8]->pvalid)
        CHECK(sink[8]->pbits == gold_sink[8]->pbits);
      event_join_occurrences(
          std::uint32_t(reset), std::uint32_t(accepted[12]),
          std::uint32_t(source[12]->pbits),
          std::uint32_t(sink[8]->pvalid && sink_ready[8]->pready),
          std::uint32_t(sink[8]->pbits));
      tick_model();
      event_join_check();
    }
    event_join_finish();
  });
}
