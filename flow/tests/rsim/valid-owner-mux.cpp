// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = 1;
    tick_model();
    reset = 0;
    first_in.pbits = 0xa5;
    second_in.pbits = 0x3c;
    select_pair = 1;
    for (bool active : {false, true}) {
      for (bool owner : {true, false}) {
        select_first = owner;
        first_in.pvalid = active && owner;
        second_in.pvalid = active && !owner;
        eval();
        CHECK(result_out.pvalid == active);
        CHECK(result_out.pbits == (owner ? 0xa5 : 0x3c));
        tick_model();
      }
    }

    select_pair = 0;
    first_in.pvalid = second_in.pvalid = 0;
    third_in.pvalid = 1;
    third_in.pbits = 0x71;
    eval();
    CHECK(result_out.pvalid && result_out.pbits == 0x71);
    tick_model();
    select_pair = 1;
    third_in.pvalid = 0;

    for (bool owner : {false, true}) {
      for (bool both : {false, true}) {
        select_first = owner;
        first_in.pvalid = !owner || both;
        second_in.pvalid = owner || both;
        eval();
        expect_failure(owner ? "valid_owner_mux_second_not_selected" :
                               "valid_owner_mux_first_not_selected", [] { tick_model(); });
      }
    }
  });
}
