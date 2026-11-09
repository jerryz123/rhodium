// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    const std::array<unsigned, 3> sparse{1, 7, 29};
    reset = 1;
    tick_model();
    reset = 0;
    for (unsigned index = 0; index < 3; ++index) {
      selector = sparse[index];
      automatic = index;
      lane = 1u << index;
      for (unsigned left : {0u, 1u, 0x345u, 0xfffu}) {
        for (unsigned right : {0u, 7u, 0x678u, 0xfffu}) {
          a = left;
          b = right;
          eval();
          const auto expected = index == 0 ? left : index == 1 ? right : (left + right) & 0xfff;
          CHECK(block_result == expected && list_result == expected && generated_result == expected);
          CHECK(payload_result == expected && automatic_result == expected && lane_result == expected);
          CHECK(single_result == left);
          CHECK(field_result == left);
          CHECK(selected == sparse[(index + 2) % 3] && selected_last == (index == 0));
          CHECK(fallback == (index == 0 ? left : 0xabc));
        }
      }
      tick_model();
      CHECK(held_result == block_result);
    }
    // A partial lookup's explicit recovery remains defined on unused encodings.
    for (unsigned raw = 0; raw < 32; ++raw) {
      selector = raw;
      eval();
      CHECK(fallback == (raw == 1 ? a : 0xabc));
    }
  });
}
