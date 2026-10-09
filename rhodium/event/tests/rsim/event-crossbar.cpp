// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

extern "C" void crossbar_bind();
extern "C" void crossbar_sample(unsigned lane, unsigned rst,
                                unsigned valid_mask, unsigned ready_mask,
                                unsigned payloads, unsigned out_valid,
                                unsigned out_ready, unsigned out_payloads,
                                unsigned grants);
extern "C" void crossbar_check();
extern "C" void crossbar_finish();
int main() {
  return run_test([] {
    auto source_ready =
        std::array{&sources_0_out, &sources_1_out, &sources_2_out,
                   &sources_3_out, &sources_4_out, &sources_5_out};
    auto sink =
        std::array{&sinks_0_out, &sinks_1_out, &sinks_2_out, &sinks_3_out};
    std::array<std::remove_reference_t<decltype(sources_0_in)>, 6> source{};
    std::array<std::remove_reference_t<decltype(sinks_0_in)>, 4> sink_ready{};
    std::array<bool, 6> accepted{};
    crossbar_bind();
    for (unsigned i = 0; i < source.size(); ++i) {
      source[i] = {};
      accepted[i] = 1;
    }
    for (unsigned step = 0; step < 220; ++step) {
      reset = step == 0 || step == 60;
      grants = {};
      if (step % 11 != 0) {
        grants[step % 3][0] = true;
        if (step % 7 != 0)
          grants[(step + 1) % 3][1] = true;
      }
      if (step == 1) {
        grants = {};
        grants[0][0] = true;
        grants[1][1] = true;
      }
      for (unsigned i = 0; i < source.size(); ++i) {
        if (accepted[i] || !source[i].pvalid || reset) {
          source[i].pvalid = step < 180 && (step < 4 || step % 9 != (i % 3) ||
                                            (step >= 48 && step <= 60));
          // Frequent identical values prevent payload-based occurrence matching.
          source[i].pbits =
              step % 4 == 0 ? (std::uint32_t(step + i % 3) & 255u) : 0x2au;
        }
      }
      for (unsigned i = 0; i < sink_ready.size(); ++i)
        sink_ready[i].pready =
            step == 1 || step >= 180 ||
            ((step % (4 + i % 2) != 1) && !(step >= 48 && step <= 60));
      sources_0_in = source[0];
      sources_1_in = source[1];
      sources_2_in = source[2];
      sources_3_in = source[3];
      sources_4_in = source[4];
      sources_5_in = source[5];
      sinks_0_in = sink_ready[0];
      sinks_1_in = sink_ready[1];
      sinks_2_in = sink_ready[2];
      sinks_3_in = sink_ready[3];
      eval();
      unsigned packed_grants = 0;
      for (unsigned input = 0; input < 3; ++input)
        for (unsigned output = 0; output < 2; ++output)
          packed_grants |= unsigned(grants[input][output])
                           << (input * 2 + output);
      if (!reset) {
        for (unsigned i = 0; i < 3; ++i)
          CHECK(source_ready[i]->pready == source_ready[i + 3]->pready);
        for (unsigned i = 0; i < 2; ++i)
          CHECK(sink[i]->pvalid == sink[i + 2]->pvalid &&
                (!sink[i]->pvalid || sink[i]->pbits == sink[i + 2]->pbits));
      }
      for (unsigned lane = 0; lane < 2; ++lane) {
        std::uint32_t valid_mask, ready_mask, payloads, out_valid, out_ready,
            out_payloads;
        valid_mask = 0;
        ready_mask = 0;
        payloads = 0;
        out_valid = 0;
        out_ready = 0;
        out_payloads = 0;
        for (unsigned i = 0; i < 3; ++i) {
          valid_mask |= std::uint32_t(source[lane * 3 + i].pvalid) << i;
          ready_mask |= std::uint32_t(source_ready[lane * 3 + i]->pready) << i;
          payloads |= std::uint32_t(source[lane * 3 + i].pbits) << (i * 8);
          accepted[lane * 3 + i] =
              source[lane * 3 + i].pvalid && source_ready[lane * 3 + i]->pready;
        }
        for (unsigned i = 0; i < 2; ++i) {
          out_valid |= std::uint32_t(sink[lane * 2 + i]->pvalid) << i;
          out_ready |= std::uint32_t(sink_ready[lane * 2 + i].pready) << i;
          out_payloads |= std::uint32_t(sink[lane * 2 + i]->pbits) << (i * 8);
        }
        crossbar_sample(std::uint32_t(lane), std::uint32_t(reset), valid_mask,
                        ready_mask, payloads, out_valid, out_ready,
                        out_payloads, packed_grants);
      }
      tick_model();
      crossbar_check();
    }
    crossbar_finish();
  });
}
