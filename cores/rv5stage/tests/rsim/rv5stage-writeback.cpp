// Checks future-cycle collisions, simultaneous reservations, shifting, and reset.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
std::uint16_t expected = 0;
unsigned accepted = 0, blocked = 0, simultaneous = 0;
auto requests() {
  return std::array{&reserve_0_in, &reserve_1_in, &reserve_2_in};
}
auto grants() {
  return std::array{&reserve_0_out, &reserve_1_out, &reserve_2_out};
}
void step() {
  eval();
  if (reset)
    expected = 0;
  else {
    CHECK(occupied == (expected & 255));
    auto next = expected;
    unsigned count = 0;
    for (unsigned i = 0; i < 3; ++i) {
      if (i == probe_client)
        CHECK(bool(probe_available) ==
              (!pause && !(next & (1u << probe_delay))));
      CHECK(bool(grants()[i]->pready) ==
            (!pause && !(next & (1u << requests()[i]->pbits))));
      if (requests()[i]->pvalid && grants()[i]->pready) {
        next |= 1u << requests()[i]->pbits;
        ++count;
        ++accepted;
      } else if (requests()[i]->pvalid)
        ++blocked;
    }
    if (count > 1)
      ++simultaneous;
    expected = next >> 1;
  }
  tick_model();
}
int main() {
  return run_test([] {
    reset = 1;
    pause = 0;
    probe_client = 0;
    probe_delay = 1;
    for (auto r : requests())
      *r = {0, 1};
    for (unsigned i = 0; i < 3; ++i)
      step();
    reset = 0;
    for (unsigned cycle = 0; cycle < 240; ++cycle) {
      pause = cycle % 23 >= 19;
      for (unsigned i = 0; i < 3; ++i)
        *requests()[i] = {
            std::uint8_t(cycle % 7 != i),
            std::uint8_t(1 + ((cycle / 5 + i * (cycle % 3)) % 8))};
      probe_client = cycle % 3;
      probe_delay = 1 + ((cycle / 3 + 2) % 8);
      step();
      if (cycle == 111) {
        reset = 1;
        step();
        step();
        reset = 0;
      }
    }
    for (auto r : requests())
      *r = {0, 1};
    probe_delay = 1;
    for (unsigned i = 0; i < 9; ++i)
      step();
    CHECK(occupied == 0 && accepted > 30 && blocked > 30 && simultaneous > 10);
  });
}
