// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <array>
#include <random>

int main() {
  return run_test([] {
    constexpr std::array<unsigned, 3> sizes{1, 3, 5};
    std::array<unsigned, 3> saved{};
    std::mt19937 rng(0x52455441);
    reset = 1;
    tick_model();
    reset = 0;

    // Randomize candidate/event combinations from both free and retained states.
    // Repeated sequences also exercise occupied capture, bubbles, and release
    // collisions without deriving the oracle from the hardware's encoded state.
    for (unsigned cycle = 0; cycle < 6000; ++cycle) {
      reset = cycle % 97 == 96;
      for (unsigned i = 0; i < sizes.size(); ++i) {
        unsigned lane = rng() % (sizes[i] + 1);
        candidates[i] = lane == sizes[i] ? 0 : 1U << lane;
        captures[i] = (rng() & 1) && (saved[i] || candidates[i]);
        releases[i] = rng() & 1;
      }
      eval();
      for (unsigned i = 0; i < sizes.size(); ++i) {
        CHECK(selections[i] == (saved[i] ? saved[i] : candidates[i]));
        CHECK(retained[i] == saved[i]);
        CHECK(held[i] == bool(saved[i]));
        if (reset || releases[i]) saved[i] = 0;
        else if (captures[i] && !saved[i]) saved[i] = candidates[i];
      }
      tick_model();
      for (unsigned i = 0; i < sizes.size(); ++i) {
        CHECK(selections[i] == (saved[i] ? saved[i] : candidates[i]));
        CHECK(retained[i] == saved[i]);
        CHECK(held[i] == bool(saved[i]));
      }
    }

    reset = 1;
    candidates = {};
    captures = {};
    releases = {};
    tick_model();
    reset = 0;
    captures[1] = 1;
    expect_failure("retained_selection_capture_present", [] { tick_model(); });
  });
}
