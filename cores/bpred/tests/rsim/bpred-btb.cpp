// Checks compact associative matching, counters, entry/page replacement, and
// invalidation.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

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
                    prediction.ptarget == target &&
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
  });
}
