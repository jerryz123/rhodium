// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

extern "C" void partial_bind();
extern "C" void partial_sample(unsigned rst, unsigned inputs, unsigned choice,
                               unsigned selected, unsigned joined,
                               unsigned middle, unsigned after_fire,
                               unsigned isolated, unsigned stalled,
                               unsigned middle_stalled,
                               unsigned isolated_stalled);
extern "C" void partial_check();
extern "C" void partial_finish();
int main() {
  return run_test([] {
    auto source_ready =
        std::array{&ingress_0_out, &ingress_1_out, &ingress_2_out,
                   &ingress_3_out, &ingress_4_out};
    auto gold_ready = std::array{&gold_ingress_0_out, &gold_ingress_1_out,
                                 &gold_ingress_2_out, &gold_ingress_3_out,
                                 &gold_ingress_4_out};
    auto sink = std::array{&egress_0_out, &egress_1_out};
    auto gold_sink = std::array{&gold_egress_0_out, &gold_egress_1_out};
    std::array<std::remove_reference_t<decltype(ingress_0_in)>, 5> source{};
    std::array<std::remove_reference_t<decltype(egress_0_in)>, 2> sink_ready{};
    partial_bind();
    for (unsigned step = 0; step < 270; ++step) {
      unsigned mask;
      reset = step == 0 || step == 120;
      for (unsigned i = 0; i < 3; ++i)
        grants[i] = i == step % 3;
      for (unsigned i = 0; i < 5; ++i) {
        source[i].pvalid = (step < 240 || i == 3) && (step % 7 != i);
        source[i].pbits = 0x2au;
      }
      sink_ready[0].pready = step >= 240 || step % 9 < 5;
      sink_ready[1].pready = step >= 240 || step % 5 < 3;
      ingress_0_in = source[0];
      gold_ingress_0_in = source[0];
      ingress_1_in = source[1];
      gold_ingress_1_in = source[1];
      ingress_2_in = source[2];
      gold_ingress_2_in = source[2];
      ingress_3_in = source[3];
      gold_ingress_3_in = source[3];
      ingress_4_in = source[4];
      gold_ingress_4_in = source[4];
      egress_0_in = sink_ready[0];
      gold_egress_0_in = sink_ready[0];
      egress_1_in = sink_ready[1];
      gold_egress_1_in = sink_ready[1];
      eval();
      mask = 0;
      for (unsigned i = 0; i < 5; ++i) {
        if (!reset)
          CHECK(source_ready[i]->pready == gold_ready[i]->pready);
        if (source[i].pvalid && source_ready[i]->pready)
          mask |= 1 << i;
      }
      for (unsigned i = 0; i < 2; ++i)
        if (!reset)
          CHECK(sink[i]->pvalid == gold_sink[i]->pvalid &&
                (!sink[i]->pvalid || sink[i]->pbits == gold_sink[i]->pbits));
      partial_sample(std::uint32_t(reset), mask, std::uint32_t(step % 3),
                     std::uint32_t(selected_fire), std::uint32_t(joined_fire),
                     std::uint32_t(middle_fire),
                     std::uint32_t(sink[0]->pvalid && sink_ready[0].pready),
                     std::uint32_t(sink[1]->pvalid && sink_ready[1].pready),
                     std::uint32_t(sink[0]->pvalid && !sink_ready[0].pready),
                     std::uint32_t(middle_stall),
                     std::uint32_t(sink[1]->pvalid && !sink_ready[1].pready));
      tick_model();
      partial_check();
    }
    partial_finish();
  });
}
