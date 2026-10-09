// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

extern "C" void branching_bind();
extern "C" void branching_sample(unsigned lane, unsigned rst, unsigned valid,
                                 unsigned ready, unsigned payloads,
                                 unsigned out_valid, unsigned out_ready,
                                 unsigned out_payloads, unsigned grants);
extern "C" void branching_check();
extern "C" void branching_finish();
int main() {
  return run_test([] {
    auto source_ready =
        std::array{&ingress_0_out, &ingress_1_out, &ingress_2_out,
                   &ingress_3_out, &ingress_4_out, &ingress_5_out};
    auto sink = std::array{&egress_0_out, &egress_1_out, &egress_2_out,
                           &egress_3_out, &egress_4_out, &egress_5_out};
    std::array<std::remove_reference_t<decltype(ingress_0_in)>, 2> source{};
    std::array<std::remove_reference_t<decltype(egress_0_in)>, 2> sink_ready{};
    branching_bind();
    for (unsigned step = 0; step < 520; ++step) {
      reset = step == 0 || step == 130 || step == 350;
      grants = {};
      for (unsigned i = 0; i < 3; ++i)
        if (step >= 460 || (step % 13 != 0 && step % 9 != i))
          grants[i][(i + step) % 3] = true;
      for (unsigned i = 0; i < source.size(); ++i) {
        source[i].pvalid = step < 460;
        source[i].pbits = 0x2au;
        sink_ready[i].pready = step >= 460 || (step % (5 + i) < 3 &&
                                               !(step >= 124 && step <= 130));
      }
      if (step >= 1 && step <= 6) {
        grants = {};
        for (unsigned i = 0; i < source.size(); ++i) {
          source[i].pvalid = step == 1;
          sink_ready[i].pready = step == 6;
        }
        if (step == 2) {
          grants[0][2] = true;
          grants[1][0] = true;
        }
        if (step == 3)
          grants[2][2] = true;
        if (step == 4)
          grants[2][1] = true;
      }
      ingress_0_in = source[0];
      ingress_1_in = source[1];
      ingress_2_in = source[0];
      ingress_3_in = source[1];
      ingress_4_in = source[0];
      ingress_5_in = source[1];
      egress_0_in = sink_ready[0];
      egress_1_in = sink_ready[1];
      egress_2_in = sink_ready[0];
      egress_3_in = sink_ready[1];
      egress_4_in = sink_ready[0];
      egress_5_in = sink_ready[1];
      eval();
      unsigned packed_grants = 0;
      for (unsigned input = 0; input < 3; ++input)
        for (unsigned output = 0; output < 3; ++output)
          packed_grants |= unsigned(grants[input][output])
                           << (input * 3 + output);
      if (!reset)
        for (unsigned lane = 0; lane < 2; ++lane)
          for (unsigned i = 0; i < 2; ++i) {
            CHECK(source_ready[lane * 2 + i]->pready ==
                  source_ready[4 + i]->pready);
            CHECK(sink[lane * 2 + i]->pvalid == sink[4 + i]->pvalid &&
                  (!sink[4 + i]->pvalid ||
                   sink[lane * 2 + i]->pbits == sink[4 + i]->pbits));
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
        for (unsigned i = 0; i < 2; ++i) {
          valid_mask |= std::uint32_t(source[i].pvalid) << i;
          ready_mask |= std::uint32_t(source_ready[lane * 2 + i]->pready) << i;
          payloads |= std::uint32_t(source[i].pbits) << (8 * i);
          out_valid |= std::uint32_t(sink[lane * 2 + i]->pvalid) << i;
          out_ready |= std::uint32_t(sink_ready[i].pready) << i;
          out_payloads |= std::uint32_t(sink[lane * 2 + i]->pbits) << (8 * i);
        }
        branching_sample(std::uint32_t(lane), std::uint32_t(reset), valid_mask,
                         ready_mask, payloads, out_valid, out_ready,
                         out_payloads, packed_grants);
      }
      tick_model();
      branching_check();
    }
    branching_finish();
  });
}
