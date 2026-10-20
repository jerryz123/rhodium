// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

constexpr std::uint64_t first = 0x0123456789abcdef,
                        superseded = 0x1111222233334444,
                        latest = 0xfedcba9876543210;
unsigned beats = 0;

void step() {
  eval();
  if (!reset && transferred) {
    const auto snapshot = beats < 8 ? first : latest;
    CHECK(beats < 16 && offered.phart == (beats / 4) % 2);
    CHECK(offered.pflit.pfirst == (beats % 4 == 0));
    CHECK(offered.pflit.plast == (beats % 4 == 3));
    CHECK(offered.pflit.ppayload ==
          ((snapshot >> (16 * (beats % 4))) & 0xffff));
    ++beats;
  }
  tick_model();
  if (!reset) {
    CHECK(hart_time[0] == (beats >= 12 ? latest : beats >= 4 ? first : 0));
    CHECK(hart_time[1] == (beats >= 16 ? latest : beats >= 8 ? first : 0));
  }
  CHECK(test_cycles < 200);
}

template <class Predicate> void until(Predicate predicate) {
  eval();
  while (!predicate())
    step();
}
void update(std::uint64_t value) {
  update_in.pvalid = 1;
  update_in.pbits = value;
  step();
  update_in.pvalid = 0;
  eval();
}
int main() {
  return run_test([] {
    reset = 1;
    step();
    step();
    reset = 0;
    update(first);
    until([] { return offer_valid; });
    auto held = offered;
    const auto stable = [&] {
      CHECK(offered.phart == held.phart &&
            offered.pflit.pfirst == held.pflit.pfirst &&
            offered.pflit.plast == held.pflit.plast &&
            offered.pflit.ppayload == held.pflit.ppayload);
    };
    update(superseded);
    update(latest);
    for (int i = 0; i < 3; ++i) {
      step();
      stable();
      CHECK(offer_valid && !transferred && hart_time[0] == 0 &&
            hart_time[1] == 0);
    }
    accepting[0] = 1;
    until([] { return beats == 4; });
    CHECK(hart_time[0] == first && hart_time[1] == 0);
    until([] { return offer_valid && offered.phart == 1; });
    held = offered;
    for (int i = 0; i < 3; ++i) {
      step();
      stable();
      CHECK(offer_valid && beats == 4 && hart_time[0] == first &&
            hart_time[1] == 0);
    }
    accepting[1] = 1;
    until([] { return beats == 8; });
    CHECK(hart_time[0] == first && hart_time[1] == first);
    until([] { return beats == 16; });
    CHECK(hart_time[0] == latest && hart_time[1] == latest);
    for (int i = 0; i < 12; ++i)
      step();
    CHECK(!offer_valid && beats == 16);
  });
}
