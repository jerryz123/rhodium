// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

extern "C" void feedback_bind();
extern "C" void feedback_sample(unsigned rst, unsigned select_feedback,
                                unsigned route_feedback, unsigned valid,
                                unsigned ready, unsigned payload,
                                unsigned out_valid, unsigned out_ready,
                                unsigned out_payload);
extern "C" void feedback_check();
extern "C" void feedback_finish();
int main() {
  return run_test([] {
    auto source_ready = std::array{&ingress_0_out, &ingress_1_out,
                                   &ingress_2_out, &ingress_3_out};
    auto sink = std::array{&egress_0_out, &egress_1_out, &egress_2_out};
    std::remove_reference_t<decltype(ingress_0_in)> source{};
    std::remove_reference_t<decltype(egress_0_in)> sink_ready{};
    feedback_bind();
    for (unsigned step = 0; step < 330; ++step) {
      reset = step == 0 || step == 9 || step == 60 || step == 180;
      source.pvalid = step < 300;
      source.pbits =
          0x2au; // Identical payloads deliberately cannot identify parents.
      select_feedback = step < 300 && step % 7 < 3;
      route_feedback = step < 300 && step % 11 < 5;
      sink_ready.pready = step >= 300 || step % 5 < 3;
      if (step >= 1 && step <= 8) {
        select_feedback = step == 2 || step == 3 || step == 8;
        route_feedback = select_feedback;
        sink_ready.pready = step == 5;
        source.pvalid = step != 4;
      }
      ingress_0_in = source;
      ingress_1_in = source;
      ingress_2_in = source;
      ingress_3_in = source;
      egress_0_in = sink_ready;
      egress_1_in = sink_ready;
      egress_2_in = sink_ready;
      eval();
      if (!reset) {
        CHECK(source_ready[0]->pready == source_ready[1]->pready);
        CHECK(sink[0]->pvalid == sink[1]->pvalid &&
              (!sink[0]->pvalid || sink[0]->pbits == sink[1]->pbits));
        CHECK((!source.pvalid ||
               (source_ready[2]->pready == source_ready[1]->pready &&
                source_ready[3]->pready == source_ready[1]->pready)) &&
              sink[2]->pvalid == sink[1]->pvalid &&
              (!sink[2]->pvalid || sink[2]->pbits == sink[1]->pbits));
      }
      feedback_sample(
          std::uint32_t(reset), std::uint32_t(select_feedback),
          std::uint32_t(route_feedback), std::uint32_t(source.pvalid),
          std::uint32_t(source_ready[0]->pready), std::uint32_t(source.pbits),
          std::uint32_t(sink[0]->pvalid), std::uint32_t(sink_ready.pready),
          std::uint32_t(sink[0]->pbits));
      tick_model();
      feedback_check();
    }
    feedback_finish();
  });
}
