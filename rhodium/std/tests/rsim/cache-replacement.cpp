// Verifies direct, tree-PLRU, invalid-way priority, and padded-leaf behavior.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void touch_four(std::uint8_t way) {
  {
    touch_four_way = way;
    touch_four_valid = 1;
    tick_model();
    touch_four_valid = 0;
  }
}

void touch_three(std::uint8_t way) {
  {
    touch_three_way = way;
    touch_three_valid = 1;
    tick_model();
    touch_three_valid = 0;
  }
}

int main() {
  return run_test([] {
    reset = 1;
    valid_one = UINT64_C(1);
    touch_one_valid = 0;
    valid_four = UINT64_C(15);
    touch_four_valid = 0;
    touch_four_way = 0;
    valid_three = UINT64_C(7);
    touch_three_valid = 0;
    touch_three_way = 0;
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    reset = 0;

    CHECK(victim_one == 0 && state_one == 0);
    touch_one_valid = 1;
    tick_model();
    touch_one_valid = 0;
    CHECK(victim_one == 0 && state_one == 0);
    valid_one = UINT64_C(0);
    eval();
    CHECK(victim_one == 0);

    CHECK(victim_four == 0 && victim_three == 0);

    touch_four(0);
    CHECK(victim_four == 2);
    touch_four(2);
    CHECK(victim_four == 1);
    touch_four(1);
    CHECK(victim_four == 3);
    touch_four(3);
    CHECK(victim_four == 0);

    valid_four = UINT64_C(10);
    eval();
    CHECK(victim_four == 0);
    valid_four = UINT64_C(11);
    eval();
    CHECK(victim_four == 2);
    valid_four = UINT64_C(15);

    touch_three(0);
    CHECK(victim_three == 2);
    touch_three(2);
    CHECK(victim_three == 1);
    touch_three(1);
    CHECK(victim_three == 2);
    CHECK(victim_three < 3);
  });
}
