// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void train(unsigned pc, bool conditional, bool taken) {
  update_in = {1, {pc, pc + 256, 1, conditional, taken, 1, 0, 0, pc + 2}};
  tick_model();
  update_in = {};
}
void check(unsigned pc, int selected) {
  cursor = pc;
  eval();
  CHECK(prediction.pvalid == (selected >= 0));
  if (selected >= 0) {
    CHECK(prediction.ppc == unsigned(selected));
    CHECK(prediction.ptarget == unsigned(selected + 256));
  }
}
int main() {
  return run_test([] {
    reset = 1;
    update_in = {}; discover_in = {}; invalidate_in = {}; invalidate_all_in = {};
    conditional_taken = {};
    tick_model(); tick_model(); reset = 0;
    for (unsigned pc : {0x106u, 0x104u, 0x102u, 0x100u}) train(pc, true, true);
    check(0x100, -1); // Caller not-taken overrides locally taken entries.
    for (unsigned bank = 0; bank < 4; ++bank) {
      conditional_taken = {};
      conditional_taken[bank] = 1;
      check(0x100, 0x100 + bank * 2);
      check(0x100 + bank * 2, 0x100 + bank * 2);
      check(0x102 + bank * 2, -1);
    }
    conditional_taken = {1, 1, 1, 1};
    check(0x100, 0x100);
    check(0x104, 0x104);
    // Preserve a target whose private counter was trained strongly not-taken.
    for (unsigned i = 0; i < 4; ++i) train(0x100, true, false);
    check(0x100, 0x100);
    conditional_taken = {};
    train(0x104, false, true);
    check(0x100, 0x104); // Unconditional target ignores caller direction mask.
    check(0x108, -1);
    cursor = UINT64_C(0x100000100);
    eval(); CHECK(!prediction.pvalid);
    invalidate_in = {1, 0x104}; tick_model(); invalidate_in = {};
    check(0x100, -1);
    conditional_taken = {1, 1, 1, 1};
    invalidate_all_in.pvalid = 1;
    check(0x100, -1);
    tick_model(); invalidate_all_in = {};
    check(0x100, -1);
  });
}
