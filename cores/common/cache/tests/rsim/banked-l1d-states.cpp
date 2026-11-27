// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    constexpr unsigned sets = 16, ways = 4;
    constexpr unsigned shared_clean = 1, unique_clean = 2, shared_dirty = 3, unique_dirty = 4;
    std::array<std::array<unsigned, ways>, sets> expected{};
    reset = 1;
    tick_model();
    reset = 0;

    auto cycle = [&](bool service, unsigned s, bool demand, unsigned d,
                     unsigned mark_set, unsigned marks, unsigned replace = 0) {
      auto &sr = states_in.prequest;
      auto &dr = demand_states_in.prequest;
      sr = {}; dr = {};
      sr.pvalid = service; sr.pbits.paddress = s;
      dr.pvalid = demand; dr.pbits.paddress = d;
      sr.pbits.pwrite = replace != 0;
      sr.pbits.pmask = replace;
      dirty_set = mark_set;
      dirty_ways = marks;
      for (unsigned w = 0; w < ways; ++w) {
        const auto clean = ((s ^ w) & 1) ? shared_clean : unique_clean;
        sr.pbits.pdata[w] = clean;
        if (service && (replace & (1u << w))) expected[s][w] = clean;
        if (marks & (1u << w)) {
          auto &state = expected[mark_set][w];
          state = (state == shared_clean || state == shared_dirty) ? shared_dirty : unique_dirty;
        }
      }
      tick_model(); eval();
      auto check = [&](const auto &response, bool read, unsigned set) {
        CHECK(bool(response.pvalid) == read);
        if (read) for (unsigned w = 0; w < ways; ++w)
          CHECK(response.pbits[w] == expected[set][w]);
      };
      check(states_out.presponse, service && !replace, s);
      check(demand_states_out.presponse, demand, d);
    };

    for (unsigned s = 0; s < sets; ++s) cycle(true, s, false, s, 0, 0, 15);
    for (unsigned s = 0; s < sets; ++s) {
      const unsigned other_bank = s ^ 1, same_bank = s ^ 4;
      // Either reader may collide while the other bank returns its own row.
      cycle(true, s, true, other_bank, s, 1);
      cycle(true, other_bank, true, s, s, 2);
      // The ungranted address still matches the mark. The winning read in the
      // same bank is a different set and must not receive a forwarded dirty bit.
      cycle(false, s, true, same_bank, s, 4);
      cycle(true, same_bank, false, s, s, 8);
      // Kill both candidates with matching bits still on the ports.
      cycle(false, s, false, s, s, 1);
      // Replacement owns a different bank; only the actual demand read forwards.
      cycle(true, other_bank, true, s, s, 8, 15);
      cycle(true, s, false, s, s, 0, 15);
      cycle(false, s, true, s, s, 0);
    }
  });
}
