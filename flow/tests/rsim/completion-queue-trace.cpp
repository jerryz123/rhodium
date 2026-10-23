// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "test-sites.h"
#include "../../../rheg/runtime/rheg.h"
#include <array>
#include <deque>
#include <vector>

struct Returned { unsigned value; rheg::Ref parent; };
struct Lane {
  std::vector<unsigned> pending;
  std::deque<Returned> buffered;
  std::array<unsigned, 4> serial{};
  unsigned outstanding = 0;
};
struct Coverage {
  unsigned requests = 0, completions = 0, replacements = 0, blocked_full = 0;
  unsigned reordered = 0, stalled = 0, simultaneous = 0, resets_pending = 0;
};

int main() {
  return run_test([] {
    rheg::graph().bind_manifest(rheg_generated::manifest());
    rheg::Graph expected;
    std::array<Lane, 2> lanes;
    std::array<Coverage, 2> coverage;
    auto requests = std::array{&requests_0_in, &requests_1_in};
    auto ready = std::array{&requests_0_out, &requests_1_out};
    auto issues = std::array{&issues_0_out, &issues_1_out};
    auto completions = std::array{&completions_0_in, &completions_1_in};
    auto responses = std::array{&responses_0_out, &responses_1_out};
    auto sinks = std::array{&responses_0_in, &responses_1_in};
    unsigned cycle = 0;
    for (unsigned step = 0; step < 400; ++step) {
      reset = step == 0 || step == 60 || step == 140;
      for (unsigned lane = 0; lane < 2; ++lane) {
        const auto &state = lanes[lane];
        requests[lane]->pvalid = step < 350 && (step < 160 || step % 7 != 0);
        requests[lane]->pbits = step & 255;
        // Accumulate requests before returning the youngest first; a completion
        // checkpoint, not request order, owns the subsequently buffered result.
        completions[lane]->pvalid = !state.pending.empty() && step % 6 >= 2;
        completions[lane]->pbits = state.pending.empty() ? 0 : state.pending.back() + 0x100;
        sinks[lane]->pready = step >= 350 || (step % 23 >= 10 && step % 5 != 0);
      }
      eval();
      if (reset) {
        for (unsigned lane = 0; lane < 2; ++lane) {
          if (lanes[lane].outstanding) ++coverage[lane].resets_pending;
          lanes[lane] = {};
        }
        expected.clear();
        cycle = 0;
      } else {
        for (unsigned lane = 0; lane < 2; ++lane) {
          auto &state = lanes[lane];
          auto &covered = coverage[lane];
          const bool out_valid = !state.buffered.empty();
          const bool response = out_valid && sinks[lane]->pready;
          const bool full = state.outstanding == 3;
          const bool request_ready = !full || (lane == 1 && response);
          const bool request = requests[lane]->pvalid && request_ready;
          const bool completion = completions[lane]->pvalid;
          CHECK(ready[lane]->pready == request_ready);
          CHECK(issues[lane]->pvalid == request);
          if (request) CHECK(issues[lane]->pbits == requests[lane]->pbits);
          CHECK(responses[lane]->pvalid == out_valid);
          if (out_valid) CHECK(responses[lane]->pbits == state.buffered.front().value);

          auto node = [&](unsigned kind, unsigned value, std::optional<rheg::Ref> parent = {}) {
            const unsigned sites[] = {completion_sites::request[lane], completion_sites::issue[lane],
                                      completion_sites::completion[lane], completion_sites::response[lane]};
            rheg::Ref ref{sites[kind], state.serial[kind]++};
            auto &entry = expected.nodes[ref];
            entry.present = true;
            entry.cycle = cycle;
            entry.width = kind < 2 ? 8 : 16;
            entry.words[0] = value;
            if (parent) expected.edges.emplace(*parent, ref);
            return ref;
          };

          if (response) {
            const auto result = state.buffered.front();
            node(3, result.value, result.parent);
            state.buffered.pop_front();
          }
          if (completion) {
            CHECK(!state.pending.empty());
            if (state.pending.size() > 1) ++covered.reordered;
            state.pending.pop_back();
            const unsigned value = completions[lane]->pbits;
            state.buffered.push_back({value, node(2, value)});
            ++covered.completions;
          }
          if (request) {
            const unsigned value = requests[lane]->pbits;
            node(1, value, node(0, value));
            state.pending.push_back(value);
            ++covered.requests;
          }
          if (full && request && response) ++covered.replacements;
          if (full && requests[lane]->pvalid && !request_ready) ++covered.blocked_full;
          if (out_valid && !sinks[lane]->pready) ++covered.stalled;
          if (completion && response) ++covered.simultaneous;
          state.outstanding += unsigned(request);
          state.outstanding -= unsigned(response);
          CHECK(state.outstanding == state.pending.size() + state.buffered.size());
        }
      }
      tick_model();
      CHECK(rheg::graph().json() == expected.json());
      if (!reset) ++cycle;
    }
    for (unsigned lane = 0; lane < 2; ++lane) {
      const auto &covered = coverage[lane];
      CHECK(lanes[lane].outstanding == 0);
      CHECK(covered.requests > 40 && covered.completions > 40);
      CHECK(covered.reordered && covered.blocked_full && covered.stalled && covered.simultaneous);
      CHECK(covered.resets_pending == 2);
      CHECK(lane == 1 ? covered.replacements > 0 : covered.replacements == 0);
    }
  });
}
