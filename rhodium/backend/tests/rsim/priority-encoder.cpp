// Checks all five-lane request patterns through the direct rsim model.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
int main() {
  return run_test([] {
    for (unsigned value = 0; value < 32; ++value) {
      unsigned index = 0, onehot = 0;
      for (unsigned i = 0; i < 5; ++i)
        if ((value >> i & 1) && !onehot) {
          index = i;
          onehot = 1u << i;
        }
      requests = value;
      eval();
      CHECK(selected_index == index);
      CHECK(selected_oh == onehot);
      CHECK(selected_two_index == ((value & 1) ? 0 : ((value >> 1) & 1)));
      CHECK(selected_two_oh == ((value & 1) ? 1 : (value & 2)));
      CHECK(selected_single_index == 0);
      CHECK(selected_single_oh == (value & 1));
    }
  });
}
