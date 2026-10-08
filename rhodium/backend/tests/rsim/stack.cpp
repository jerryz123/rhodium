// Checks stack behavior through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void cycle(bool next_push, bool next_pop, bool next_en,
           std::uint32_t next_data) {
  {
    push = next_push;
    pop = next_pop;
    en = next_en;
    data_in = next_data;
    tick_model();
  }
}

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    push = UINT64_C(0);
    pop = UINT64_C(0);
    en = UINT64_C(0);
    data_in = UINT64_C(0);
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    CHECK(data_out == UINT64_C(0));

    reset = UINT64_C(0);
    cycle(UINT64_C(1), UINT64_C(0), UINT64_C(1), UINT64_C(17));
    CHECK(data_out == UINT64_C(0));
    cycle(UINT64_C(0), UINT64_C(0), UINT64_C(1), UINT64_C(0));
    CHECK(data_out == UINT64_C(17));

    cycle(UINT64_C(1), UINT64_C(0), UINT64_C(1), UINT64_C(34));
    CHECK(data_out == UINT64_C(17));
    cycle(UINT64_C(0), UINT64_C(0), UINT64_C(1), UINT64_C(0));
    CHECK(data_out == UINT64_C(34));

    cycle(UINT64_C(0), UINT64_C(1), UINT64_C(1), UINT64_C(0));
    CHECK(data_out == UINT64_C(34));
    cycle(UINT64_C(0), UINT64_C(0), UINT64_C(1), UINT64_C(0));
    CHECK(data_out == UINT64_C(17));

    cycle(UINT64_C(1), UINT64_C(0), UINT64_C(0), UINT64_C(3735928559));
    cycle(UINT64_C(0), UINT64_C(0), UINT64_C(1), UINT64_C(0));
    CHECK(data_out == UINT64_C(17));

    cycle(UINT64_C(1), UINT64_C(0), UINT64_C(1), UINT64_C(34));
    cycle(UINT64_C(1), UINT64_C(0), UINT64_C(1), UINT64_C(51));
    cycle(UINT64_C(1), UINT64_C(0), UINT64_C(1), UINT64_C(68));
    cycle(UINT64_C(1), UINT64_C(0), UINT64_C(1), UINT64_C(85));
    cycle(UINT64_C(0), UINT64_C(0), UINT64_C(1), UINT64_C(0));
    CHECK(data_out == UINT64_C(68));
  });
}
