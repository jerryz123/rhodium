// Checks all shift-FIFO options against a transaction scoreboard and the
// pointer FIFO.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <deque>

int main() {
  return run_test([] {
    const std::array source{&sources_0_in, &sources_1_in,  &sources_2_in,
                            &sources_3_in, &sources_4_in,  &sources_5_in,
                            &sources_6_in, &sources_7_in,  &sources_8_in,
                            &sources_9_in, &sources_10_in, &sources_11_in};
    const std::array sink{&sinks_0_out, &sinks_1_out,  &sinks_2_out,
                          &sinks_3_out, &sinks_4_out,  &sinks_5_out,
                          &sinks_6_out, &sinks_7_out,  &sinks_8_out,
                          &sinks_9_out, &sinks_10_out, &sinks_11_out};
    const std::array source_ready{
        &sources_0_out, &sources_1_out, &sources_2_out,  &sources_3_out,
        &sources_4_out, &sources_5_out, &sources_6_out,  &sources_7_out,
        &sources_8_out, &sources_9_out, &sources_10_out, &sources_11_out};
    const std::array sink_ready{&sinks_0_in, &sinks_1_in,  &sinks_2_in,
                                &sinks_3_in, &sinks_4_in,  &sinks_5_in,
                                &sinks_6_in, &sinks_7_in,  &sinks_8_in,
                                &sinks_9_in, &sinks_10_in, &sinks_11_in};
    std::array<std::deque<std::uint8_t>, 12> model;
    std::array<bool, 12> accepted;
    accepted.fill(true);
    std::array<unsigned, 12> bypasses{}, replacements{}, stalls{}, shifts{};
    std::uint32_t random_state = 0x517f1f0;
    const auto random_word = [&] {
      random_state = random_state * 1664525u + 1013904223u;
      return random_state ^ (random_state >> 16);
    };
    const auto depth = [](unsigned lane) {
      return lane < 4 ? 1u : lane < 8 ? 2u : 5u;
    };
    for (unsigned step = 0; step < 1200; ++step) {
      reset = step == 0 || step == 90 || step == 91 || step == 700;
      flush = step == 333 || step == 334 || step == 812;
      for (unsigned i = 0; i < 12; ++i) {
        if (accepted[i] || !source[i]->pvalid || reset) {
          source[i]->pvalid =
              step < 1100 && (step < 180 || random_word() % 4 != 0);
          source[i]->pbits = std::uint8_t(random_word());
        }
        sink_ready[i]->pready =
            step >= 1100 ||
            (step < 180 ? step % 40 >= 12 : random_word() % 3 != 0);
      }
      eval();
      // Capture every transfer before tick publishes the next state's outputs.
      for (unsigned i = 0; i < 12; ++i) {
        const bool piped = i % 4 >= 2, flowed = i % 2 == 1;
        const bool expected_ready =
            model[i].size() < depth(i) || (piped && sink_ready[i]->pready);
        const bool expected_valid =
            !model[i].empty() || (flowed && source[i]->pvalid);
        const auto expected_bits =
            model[i].empty() ? source[i]->pbits : model[i].front();
        if (!reset) {
          CHECK(counts[i] == model[i].size() &&
                masks[i] == (1u << model[i].size()) - 1);
          CHECK(source_ready[i]->pready == expected_ready &&
                sink[i]->pvalid == expected_valid);
          if (expected_valid)
            CHECK(sink[i]->pbits == expected_bits);
          CHECK(reference_matches[i]);
        }
        const bool push = source[i]->pvalid && source_ready[i]->pready;
        const bool pop = sink[i]->pvalid && sink_ready[i]->pready;
        const bool bypass = model[i].empty() && flowed && push && pop;
        accepted[i] = push;
        if (reset || flush)
          model[i].clear();
        else {
          if (bypass)
            ++bypasses[i];
          if (model[i].size() == depth(i) && push && pop)
            ++replacements[i];
          if (sink[i]->pvalid && !sink_ready[i]->pready)
            ++stalls[i];
          if (!bypass) {
            if (pop) {
              if (model[i].size() > 1)
                ++shifts[i];
              CHECK(!model[i].empty());
              model[i].pop_front();
            }
            if (push)
              model[i].push_back(source[i]->pbits);
          }
        }
      }
      tick_model();
    }
    for (unsigned i = 0; i < 12; ++i) {
      CHECK(model[i].empty() && counts[i] == 0 && !sink[i]->pvalid);
      CHECK(stalls[i] > 0);
      if (i % 2 == 1)
        CHECK(bypasses[i] > 0);
      if (i % 4 >= 2)
        CHECK(replacements[i] > 0);
      if (depth(i) > 1)
        CHECK(shifts[i] > 0);
    }
  });
}
