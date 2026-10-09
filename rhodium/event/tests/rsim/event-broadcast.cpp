// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "events.h"

extern "C" void
event_broadcast_sample(unsigned lane, unsigned rst, unsigned in_valid,
                       unsigned in_ready, unsigned payload,
                       unsigned broadcast_offer, unsigned broadcast_accept,
                       unsigned branch_transfers, unsigned out_valid,
                       unsigned out_ready, unsigned payloads);
extern "C" void event_broadcast_check();
extern "C" void event_broadcast_finish();

int main() {
  return run_test([] {
    rheg::graph().bind_manifest(rheg_generated::manifest());
    auto source = std::array{&sources_0_in, &sources_1_in, &sources_2_in};
    auto source_response =
        std::array{&sources_0_out, &sources_1_out, &sources_2_out};
    auto gold_source_response = std::array{
        &gold_sources_0_out, &gold_sources_1_out, &gold_sources_2_out};
    auto broadcast_valid =
        std::array{&broadcast_valid0, &broadcast_valid1, &broadcast_valid2};
    auto broadcast_ready =
        std::array{&broadcast_ready0, &broadcast_ready1, &broadcast_ready2};
    auto transfers = std::array{&transfers0, &transfers1, &transfers2};
    auto sink = std::array{&sinks_0_out, &sinks_1_out, &sinks_2_out,
                           &sinks_3_out, &sinks_4_out, &sinks_5_out,
                           &sinks_6_out, &sinks_7_out, &sinks_8_out};
    auto sink_ready = std::array{&sinks_0_in, &sinks_1_in, &sinks_2_in,
                                 &sinks_3_in, &sinks_4_in, &sinks_5_in,
                                 &sinks_6_in, &sinks_7_in, &sinks_8_in};
    auto gold_sink =
        std::array{&gold_sinks_0_out, &gold_sinks_1_out, &gold_sinks_2_out,
                   &gold_sinks_3_out, &gold_sinks_4_out, &gold_sinks_5_out,
                   &gold_sinks_6_out, &gold_sinks_7_out, &gold_sinks_8_out};
    auto gold_source =
        std::array{&gold_sources_0_in, &gold_sources_1_in, &gold_sources_2_in};
    auto gold_sink_ready =
        std::array{&gold_sinks_0_in, &gold_sinks_1_in, &gold_sinks_2_in,
                   &gold_sinks_3_in, &gold_sinks_4_in, &gold_sinks_5_in,
                   &gold_sinks_6_in, &gold_sinks_7_in, &gold_sinks_8_in};
    std::array<bool, 3> accepted{};
    std::uint32_t random_state = 0xadbcec31u;
    auto random_word = [&] {
      random_state = random_state * 1664525u + 1013904223u;
      return random_state ^ (random_state >> 16);
    };
    for (unsigned i = 0; i < source.size(); ++i) {
      (*source[i]) = {};
      accepted[i] = 1;
    }
    for (unsigned step = 0; step < 500; step++) {
      reset = step == 0 || step == 100 || step == 151 || step == 152;
      for (unsigned i = 0; i < source.size(); ++i) {
        if (accepted[i] || !source[i]->pvalid || reset) {
          source[i]->pvalid = step < 380 && ((step >= 20 && step < 46) ||
                                             (step >= 70 && step <= 100) ||
                                             random_word() % 4 != 0);
          // Repeated payloads force the checker to distinguish occurrence IDs.
          source[i]->pbits = (std::uint32_t(random_word() % 4) & 255u);
        }
      }
      for (unsigned i = 0; i < sink_ready.size(); ++i) {
        sink_ready[i]->pready =
            step >= 380 ||
            ((step >= 20 && step < 46) ||
             ((step >= 70 && step <= 100) ? (i % 3 != 2)
                                          : random_word() % (i % 3 + 2) == 0));
      }
      for (unsigned i = 0; i < source.size(); ++i)
        *gold_source[i] = *source[i];
      for (unsigned i = 0; i < sink_ready.size(); ++i)
        *gold_sink_ready[i] = *sink_ready[i];
      eval();
      for (unsigned i = 0; i < source.size(); ++i) {
        std::uint32_t valid_mask, ready_mask, payloads;
        valid_mask = 0;
        ready_mask = 0;
        payloads = 0;
        CHECK(source_response[i]->pready == gold_source_response[i]->pready);
        accepted[i] = source[i]->pvalid && source_response[i]->pready;
        for (unsigned j = 0; j < 3; j++) {
          CHECK(sink[3 * i + j]->pvalid == gold_sink[3 * i + j]->pvalid);
          if (sink[3 * i + j]->pvalid)
            CHECK(sink[3 * i + j]->pbits == gold_sink[3 * i + j]->pbits);
          valid_mask |= std::uint32_t(sink[3 * i + j]->pvalid) << j;
          ready_mask |= std::uint32_t(sink_ready[3 * i + j]->pready) << j;
          payloads |= std::uint32_t(sink[3 * i + j]->pbits) << (8 * j);
        }
        event_broadcast_sample(std::uint32_t(i), std::uint32_t(reset),
                               std::uint32_t(source[i]->pvalid),
                               std::uint32_t(source_response[i]->pready),
                               std::uint32_t(source[i]->pbits),
                               std::uint32_t((*broadcast_valid[i])),
                               std::uint32_t((*broadcast_ready[i])),
                               std::uint32_t((*transfers[i])), valid_mask,
                               ready_mask, payloads);
      }
      tick_model();
      event_broadcast_check();
    }
    event_broadcast_finish();
  });
}
