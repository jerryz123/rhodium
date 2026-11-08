// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = 1;
    command = 0;
    priority = 1;
    ports::select = 0;
    tick_model();
    CHECK(held_bits == 0);
    reset = 0;
    for (unsigned code : {0u, 8u, 3u}) {
      for (unsigned lane = 0; lane < 3; ++lane) {
        for (unsigned choose = 0; choose < 2; ++choose) {
          command = code;
          priority = 1u << lane;
          ports::select = choose;
          eval();
          CHECK(selected == (choose ? 8 : 3));
          CHECK(selected_load == choose);
          CHECK(input_load == (code == 8));
          CHECK(envelope_load == (code == 8));
          CHECK(vector_load == (code == 8));
          CHECK(literal_bits == 8 && echoed_bits == code);
          CHECK(priority_bits == lane + 1);
          tick_model();
          CHECK(held_bits == code);
        }
      }
    }
  });
}
