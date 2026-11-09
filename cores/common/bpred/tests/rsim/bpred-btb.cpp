// Checks compact associative matching, counters, entry/page replacement, and
// invalidation.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "target-oracle.hpp"
#ifndef BTB_PC_WIDTH
#define BTB_PC_WIDTH 64
#endif

void train(std::uint64_t pc, std::uint64_t target, bool conditional, bool taken,
           bool compressed = 0, bool branch = 1, std::uint8_t ras_action = 0) {
  tick_model();
  update_in = {UINT64_C(1),
               {pc, target, branch, conditional, taken, compressed, ras_action,
                ras_action, pc + (compressed ? 2 : 4)}};
  tick_model();
  update_in = {};
}
void check(std::uint64_t pc, bool valid, std::uint64_t target = 0,
           std::uint8_t ras_action = 0) {
  cursor = pc;
  eval();
  CHECK(prediction.pvalid == valid &&
        (!valid || (slice(prediction.ppc, 63, 2) == slice(pc, 63, 2) &&
                    prediction.ptarget == expected_target(target) &&
                    prediction.pras_uaction == ras_action)));
}
void discover(std::uint64_t pc, std::uint64_t target, bool compressed,
              std::uint8_t ras_action) {
  tick_model();
  discover_in = {UINT64_C(1),
                 {UINT64_C(1), pc, target, compressed, ras_action}};
  tick_model();
  discover_in = {};
}
int main() {
  return run_test([] {
    reset = 1;
    cursor = 0;
    invalidate_all_in = {};
    update_in = {};
    discover_in = {};
    invalidate_in = {};

    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    reset = 0;
    check(UINT64_C(0x100), 0);
    train(UINT64_C(0x100), UINT64_C(0x200), 1, 0);
    check(UINT64_C(0x100), 0);
    train(UINT64_C(0x100), UINT64_C(0x200), 1, 1, 1);
    check(UINT64_C(0x100), 1, UINT64_C(0x200));
    train(UINT64_C(0x100), UINT64_C(0x200), 1, 0, 1);
    check(UINT64_C(0x100), 0);
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      train(UINT64_C(0x100), UINT64_C(0x200), 1, 0, 1);
    train(UINT64_C(0x100), UINT64_C(0x200), 1, 1, 1);
    check(UINT64_C(0x100), 0);
    train(UINT64_C(0x100), UINT64_C(0x200), 1, 1, 1);
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index)
      train(UINT64_C(0x100), UINT64_C(0x200), 1, 1, 1);
    train(UINT64_C(0x100), UINT64_C(0x200), 1, 0, 1);
    check(UINT64_C(0x100), 1, UINT64_C(0x200));
    train(UINT64_C(0x102), UINT64_C(0x302), 0, 1, 1);
    check(UINT64_C(0x100), 1, UINT64_C(0x200));
    check(UINT64_C(0x102), 1, UINT64_C(0x302));
    train(UINT64_C(0x100), UINT64_C(0x200), 1, 0, 1);
    check(UINT64_C(0x100), 1, UINT64_C(0x302));
    check(UINT64_C(0x100000100), 0);
    train(UINT64_C(0x102), UINT64_C(0x402), 0, 1, 1);
    check(UINT64_C(0x102), 1, UINT64_C(0x402));
    train(UINT64_C(0x104), UINT64_C(0x500), 0, 1);
    train(UINT64_C(0x108), UINT64_C(0x600), 0, 1);
    check(UINT64_C(0x100), 1, UINT64_C(0x402));
    check(UINT64_C(0x108), 1, UINT64_C(0x600));
    train(UINT64_C(0x102), 0, 0, 0, 0, 0);
    check(UINT64_C(0x102), 0);
    tick_model();
    invalidate_in = {UINT64_C(1), UINT64_C(264)};
    update_in = {UINT64_C(1),
                 {UINT64_C(264), UINT64_C(1792), UINT64_C(1), UINT64_C(0),
                  UINT64_C(1), UINT64_C(0), UINT64_C(0), UINT64_C(0),
                  UINT64_C(268)}};
    tick_model();
    invalidate_in = {};
    update_in = {};
    check(UINT64_C(0x108), 0);
    invalidate_all_in.pvalid = 1;
    check(UINT64_C(0x104), 0);
    tick_model();
    invalidate_all_in.pvalid = 0;
    check(UINT64_C(0x104), 0);
    train(UINT64_C(0x102), UINT64_C(0x300), 0, 1, 1);
    train(UINT64_C(0x100), UINT64_C(0x200), 0, 1, 1);
    check(UINT64_C(0x100), 1, UINT64_C(0x200));
    check(UINT64_C(0x102), 1, UINT64_C(0x300));
    train(UINT64_C(0x10c), UINT64_C(0x700), 0, 1, 0, 1, UINT64_C(2));
    check(UINT64_C(0x10c), 1, UINT64_C(0x700), UINT64_C(2));
    discover(UINT64_C(0x110), UINT64_C(0x900), 0, UINT64_C(2));
    check(UINT64_C(0x110), 1, UINT64_C(0x900), UINT64_C(2));
    tick_model();
    discover_in = {
        UINT64_C(1),
        {UINT64_C(1), UINT64_C(276), UINT64_C(2560), UINT64_C(0), UINT64_C(2)}};
    update_in = {UINT64_C(1),
                 {UINT64_C(280), UINT64_C(2816), UINT64_C(1), UINT64_C(0),
                  UINT64_C(1), UINT64_C(0), UINT64_C(0), UINT64_C(0),
                  UINT64_C(284)}};
    tick_model();
    discover_in = {};
    update_in = {};
    check(UINT64_C(0x118), 1, UINT64_C(0xb00));
    check(UINT64_C(0x114), 0);
    invalidate_all_in.pvalid = 1;
    update_in = {UINT64_C(1),
                 {UINT64_C(260), UINT64_C(1280), UINT64_C(1), UINT64_C(0),
                  UINT64_C(1), UINT64_C(0), UINT64_C(0), UINT64_C(0),
                  UINT64_C(264)}};
    tick_model();
    invalidate_all_in.pvalid = 0;
    update_in = {};
    check(UINT64_C(0x104), 0);
    train(UINT64_C(0x100), UINT64_C(0x4100), 0, 1);
    check(UINT64_C(0x100), 1, UINT64_C(0x4100));
    train(UINT64_C(0x104), UINT64_C(0x8100), 0, 1);
    check(UINT64_C(0x100), 0);
    check(UINT64_C(0x104), 1, UINT64_C(0x8100));
    train(UINT64_C(0x8100), UINT64_C(0x100), 0, 1);
    check(UINT64_C(0x8100), 1, UINT64_C(0x100));
    train(UINT64_C(0xc100), UINT64_C(0x8100), 0, 1);
    check(UINT64_C(0x104), 0);
    check(UINT64_C(0x8100), 0);
    check(UINT64_C(0xc100), 1, UINT64_C(0x8100));
    // Destinations may alias, but source tags and invalidation stay exact.
    for (auto target : {UINT64_C(0x8000000000000202), UINT64_C(0x0000100000000300),
                        UINT64_C(0xffffffc012345000), UINT64_C(0xfffffffffffffffE)}) {
      invalidate_all_in.pvalid = 1;
      tick_model();
      invalidate_all_in.pvalid = 0;
      const auto pc = BTB_PC_WIDTH == 64 ? UINT64_C(0x8000000000000100) : UINT64_C(0xfffff00000000100);
      train(pc, target, false, true);
      check(pc, true, target);
      check(0x100, false);
      invalidate_in = {1, 0x100};
      tick_model();
      invalidate_in = {};
      check(pc, true, target);
      invalidate_in = {1, pc};
      tick_model();
      invalidate_in = {};
      check(pc, false);
      discover(pc, target, false, 0);
      check(pc, true, target);
    }

#if BTB_PC_WIDTH < 64
    // Both signs, the representable limits, and the final halfword before XLEN wrap.
    constexpr auto sign = UINT64_C(1) << (BTB_PC_WIDTH - 1);
    for (auto pc : {UINT64_C(0x100), sign - 2, UINT64_C(0) - sign, UINT64_MAX - 1}) {
      invalidate_all_in.pvalid = 1;
      tick_model();
      invalidate_all_in = {};
      train(pc, 0x300, false, true, true, true, 2);
      check(pc, true, 0x300, 2);
      CHECK(prediction.ppc == pc);

      // Flipping the stored sign bit or any discarded bit must fail source matching.
      for (unsigned bit = BTB_PC_WIDTH - 1; bit < 64; ++bit) {
        const auto alias = pc ^ (UINT64_C(1) << bit);
        check(alias, false);
        invalidate_in = {1, alias};
        tick_model();
        invalidate_in = {};
        train(alias, 0x8000, false, true);
        train(alias, 0, false, false, false, false);
        discover(alias, 0xc000, false, 0);
        check(alias, false);
        check(pc, true, 0x300, 2);

        // An ignored update still suppresses discovery; any invalidation still suppresses writes.
        update_in = {1, {alias, 0x8000, 1, 0, 1, 0, 0, 0, alias + 4}};
        discover_in = {1, {1, pc, 0xc000, 0, 0}};
        tick_model();
        update_in = {}; discover_in = {};
        check(pc, true, 0x300, 2);
        invalidate_in = {1, alias};
        update_in = {1, {pc, 0x8000, 1, 0, 1, 0, 0, 0, pc + 4}};
        tick_model();
        update_in = {}; invalidate_in = {};
        check(pc, true, 0x300, 2);
      }

      invalidate_in = {1, pc};
      tick_model(); invalidate_in = {};
      check(pc, false);
      discover(pc, 0x300, true, 2);
      check(pc, true, 0x300, 2);
    }

    // Rejected source writes must not advance the entry or page replacement cursors.
    invalidate_all_in.pvalid = 1; tick_model(); invalidate_all_in = {};
    for (auto pc : {0x100u, 0x104u, 0x108u}) train(pc, pc + 0x100, false, true);
    for (unsigned i = 0; i < 7; ++i) {
      train((UINT64_C(1) << BTB_PC_WIDTH) | (0x200 + i * 4), 0x8000, false, true);
      discover((UINT64_C(1) << BTB_PC_WIDTH) | (0x300 + i * 4), 0xc000, false, 0);
    }
    train(0x10c, 0x20c, false, true);
    check(0x100, false);
    check(0x104, true, 0x204);
    check(0x108, true, 0x208);
    check(0x10c, true, 0x20c);

    // The target may span more bits than the source contract; it is not a source alias.
    invalidate_all_in.pvalid = 1; tick_model(); invalidate_all_in = {};
    train(0x100, UINT64_C(0x8000000000000100), false, true);
    check(0x100, true, UINT64_C(0x8000000000000100));
    check(UINT64_C(0x8000000000000100), false);
#endif
  });
}
