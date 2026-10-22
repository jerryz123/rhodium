// Checks registered/ordered queries, future-cycle collisions, pause, aging, and reset.
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
    std::array<bool, 4> ordered{};
    for (unsigned q = 0; q < query_delay.size(); ++q)
      CHECK(bool(registered_free[q]) ==
            (query_delay[q] <= 8 && !(expected & (1u << query_delay[q]))));
    unsigned count = 0;
    for (unsigned i = 0; i < 3; ++i) {
      for (unsigned q = 0; q < query_delay.size(); ++q)
        if (i == query_client[q])
          ordered[q] = query_delay[q] <= 8 && !pause && !(next & (1u << query_delay[q]));
      CHECK(bool(grants()[i]->pready) ==
            (!pause && !(next & (1u << requests()[i]->pbits))));
      if (requests()[i]->pvalid && grants()[i]->pready) {
        next |= 1u << requests()[i]->pbits;
        ++count;
        ++accepted;
      } else if (requests()[i]->pvalid)
        ++blocked;
    }
    for (unsigned q = 0; q < query_delay.size(); ++q)
      CHECK(bool(prior_claims_available[q]) == ordered[q]);
    if (count > 1)
      ++simultaneous;
    expected = next >> 1;
  }
  tick_model();
  CHECK(occupied == expected);
}
int main() {
  return run_test([] {
    reset = 1;
    pause = 0;
    query_client = {0, 1, 2, 3};
    query_delay = {0, 1, 8, 15};
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
      for (unsigned q = 0; q < query_delay.size(); ++q) {
        query_client[q] = (cycle + q) % 4;
        query_delay[q] = (cycle / 4 + q * 5) % 16;
      }
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
    query_delay = {0, 1, 8, 15};
    for (unsigned i = 0; i < 9; ++i)
      step();
    CHECK(occupied == 0 && accepted > 30 && blocked > 30 && simultaneous > 10);

    // The same-cycle winner is invisible to registered queries and its own
    // ordered query, but blocks later clients. An invalid client never grants.
    pause = 0;
    query_client = {0, 1, 2, 3};
    query_delay = {4, 4, 4, 4};
    for (auto r : requests())
      *r = {1, 4};
    eval();
    CHECK(registered_free[0] && registered_free[1] && registered_free[2] && registered_free[3]);
    CHECK(prior_claims_available[0] && !prior_claims_available[1] &&
          !prior_claims_available[2] && !prior_claims_available[3]);
    step();

    // Pause changes admission, not a registered occupancy query, and still ages bookings.
    pause = 1;
    eval();
    CHECK(registered_free[0] && !prior_claims_available[0]);
    step();
    for (auto r : requests())
      *r = {0, 1};
    for (unsigned i = 0; i < 8; ++i)
      step();
    CHECK(occupied == 0);

    for (const unsigned delay : {0u, 9u}) {
      dut = Model{};
      expect_failure("writeback_reservation_latency_in_range", [delay] {
        reset = 1;
        pause = 0;
        query_client = {};
        query_delay = {};
        for (auto r : requests())
          *r = {0, 1};
        tick_model();
        reset = 0;
        reserve_0_in = {1, std::uint8_t(delay)};
        tick_model();
      });
    }
  });
}
