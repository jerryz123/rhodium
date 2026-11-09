// Checks address-ordered prediction at every halfword cursor of an eight-byte
// fetch block.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void train(int pc, bool conditional = 0, bool taken = 1) {
  tick_model();
  update_in = {UINT64_C(1),
               {((pc)&low_mask(64)), ((pc + 256) & low_mask(64)), UINT64_C(1),
                conditional, taken, UINT64_C(1), UINT64_C(0), UINT64_C(0),
                ((pc + 2) & low_mask(64))}};
  tick_model();
  update_in = {};
}
void check(std::uint64_t pc, std::int64_t selected) {
  cursor = ((pc)&low_mask(64));
  eval();
  CHECK(prediction.pvalid == (selected >= 0) &&
        (selected < 0 ||
         (prediction.ppc == ((selected)&low_mask(64)) &&
          prediction.ptarget == ((selected + 256) & low_mask(64)))));
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

    train(UINT64_C(0x106));
    train(UINT64_C(0x104));
    train(UINT64_C(0x102));
    train(UINT64_C(0x100));
    for (int offset = 0; offset < 8; offset += 2)
      check(UINT64_C(256) + ((offset)&low_mask(64)),
            UINT64_C(256) + ((offset)&low_mask(64)));
    check(UINT64_C(0x108), -1);
    check(UINT64_C(4294967552), -1);
    tick_model();
    invalidate_in = {UINT64_C(1), UINT64_C(258)};
    tick_model();
    invalidate_in = {};
    check(UINT64_C(0x102), UINT64_C(0x104));
    train(UINT64_C(0x100), 1, 0);
    check(UINT64_C(0x100), UINT64_C(0x104));
    train(UINT64_C(0x100), 1, 1);
    check(UINT64_C(0x100), UINT64_C(0x100));
    train(UINT64_C(0x10e));
    check(UINT64_C(0x108), UINT64_C(0x10e));
    check(UINT64_C(0x10e), UINT64_C(0x10e));
    check(UINT64_C(0x110), -1);
    invalidate_all_in.pvalid = 1;
    check(UINT64_C(0x100), -1);
    tick_model();
    invalidate_all_in.pvalid = 0;
    check(UINT64_C(0x106), -1);
  });
}
