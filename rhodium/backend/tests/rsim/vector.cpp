// Checks vector behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    reset = UINT64_C(1);
    a = UINT64_C(1);
    b = UINT64_C(2);
    c = UINT64_C(3);
    alternate[0] = UINT64_C(4);
    alternate[1] = UINT64_C(5);
    alternate[2] = UINT64_C(6);
    selector = UINT64_C(0);

    eval();
    CHECK(assembled[0] == a && assembled[1] == b && assembled[2] == c);
    CHECK(packed == UINT64_C(801));
    CHECK(restored == assembled);
    CHECK(reversed[0] == c && reversed[1] == b && reversed[2] == a);
    CHECK(chosen == a);

    tick_model();
    CHECK(state_out == assembled);

    reset = UINT64_C(0);
    selector = UINT64_C(1);
    eval();
    CHECK(selected == alternate && chosen == b);
    tick_model();
    CHECK(state_out == alternate);

    selector = UINT64_C(2);
    eval();
    CHECK(selected == assembled && chosen == c);
  });
}
