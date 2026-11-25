// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "test.hpp"
#include <initializer_list>
#include <tuple>

template<class Stage, class Resolution>
void check_mem_branch_recovery(const Stage &last_stage, Resolution &last_resolution) {
  constexpr unsigned width = std::tuple_size_v<decltype(instructions_in.pbits.pentries)>;
  constexpr unsigned pc = 0x800;

  for (bool stale_prediction_on_load : {false, true}) {
    dut.inputs = {};
    reset = 1;
    memory_in.pdrained = 1;
    eval(); tick_model(); tick_model();
    reset = 0;
    instructions_in.pvalid = 1;
    instructions_in.pbits.pcount = width;
    const unsigned second_pc = pc + (stale_prediction_on_load ? 8 : 4);
    const unsigned jump_pc = second_pc + 4 * (width - 2);
    for (unsigned i = 0; i < width; ++i) {
      auto &entry = instructions_in.pbits.pentries[i];
      entry.ppc = i == 0 ? pc : second_pc + 4 * (i - 1);
      // ld x11,0(x0); optional nop; jal x17,+64
      entry.pinstruction = entry.praw_uinstruction =
          i == 0 ? 0x00003583 : i == width - 1 ? 0x040008ef : 0x13;
      entry.psequential_upc = entry.ppc + 4;
    }
    if (stale_prediction_on_load)
      instructions_in.pbits.pentries[0].pprediction = {1, pc, second_pc, 0, 0};
    for (unsigned cycles = 0;; ++cycles) {
      eval(); CHECK(cycles < 40);
      if (memory_stage_0_out.pvalid && last_stage.pvalid) break;
      const bool accepted = instructions_in.pvalid && instructions_out.pready;
      tick_model();
      if (accepted) instructions_in.pvalid = 0;
    }
    CHECK(memory_stage_0_out.pbits.ppc == pc && last_stage.pbits.ppc == jump_pc);

    // No clock edge: a late response may veto the winner, never replace it.
    for (auto [valid, outcome, permits] : std::initializer_list<std::tuple<bool, unsigned, bool>>{
             {true, 0, true}, {true, 1, true},     // Slow, LoadHit
             {true, 3, false}, {true, 4, false},   // Replay, PageFault
             {true, 5, false}, {false, 0, false},  // AccessFault, absent response
             {true, 0, true}}) {
      pipeline_in.presponse.pvalid = valid;
      pipeline_in.presponse.pbits.poutcome = outcome;
      // A fault/replay on the youngest lane must not veto an older mismatch.
      for (unsigned disposition : {0u, 1u, 2u}) {
        last_resolution.pvalid = disposition != 0;
        last_resolution.pbits.pdisposition = disposition;
        eval();
        const bool correct = permits && (stale_prediction_on_load || disposition == 0);
        CHECK(bool(redirect_out.pvalid) == correct);
        CHECK(bool(fetch_flush_out.pvalid) == correct);
        if (correct) {
          CHECK(redirect_out.pbits.ppc == (stale_prediction_on_load ? pc : jump_pc));
          CHECK(redirect_out.pbits.ptarget == (stale_prediction_on_load ? pc + 4 : jump_pc + 64));
        }
      }
    }
    if constexpr (width == 3) {
      // The middle slot is younger than a stale load, but older than the JAL.
      last_resolution.pvalid = 0;
      for (unsigned disposition : {1u, 2u}) {
        resolution_1_in.pvalid = 1;
        resolution_1_in.pbits.pdisposition = disposition;
        eval();
        CHECK(bool(redirect_out.pvalid) == stale_prediction_on_load);
        CHECK(bool(fetch_flush_out.pvalid) == stale_prediction_on_load);
        if (stale_prediction_on_load)
          CHECK(redirect_out.pbits.ppc == pc && redirect_out.pbits.ptarget == pc + 4);
      }
    }
  }
  dut.inputs = {};
  reset = 1;
  eval(); tick_model();
}
