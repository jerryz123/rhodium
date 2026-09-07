// Checks queue lineage from public transfers without using hardware storage controls.
#include "../../../rhodium/event/runtime/rhodium_event.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <deque>

namespace {
struct Pending { std::uint64_t parent, cycle; unsigned payload; };
struct Lane {
  std::deque<Pending> pending;
  std::array<std::uint64_t, 2> sequences{};
};
struct Coverage {
  unsigned accepted = 0, stored = 0, bypass = 0, full_replace = 0;
  unsigned stalls = 0, bubbles = 0, flushes = 0, full = 0;
};
std::array<Lane, 10> lanes;
std::array<Coverage, 10> coverage;
rhodium_event::Graph expected;
std::uint64_t cycle = 0;
bool in_reset = true;

[[noreturn]] void fail(const std::string& message) {
  std::fprintf(stderr, "%s\n", message.c_str());
  std::abort();
}

std::uint64_t node(unsigned lane, unsigned local_site, unsigned payload,
                   std::uint64_t parent = 0) {
  const auto sequence = lanes[lane].sequences[local_site]++;
  const auto site = lane * 2 + local_site;
  auto& value = expected.nodes[{site, sequence}];
  value.present = true;
  value.width = 8;
  value.cycle = cycle;
  value.words[0] = payload;
  if (local_site == 1) expected.edges.insert({{lane * 2, parent}, {site, sequence}});
  return sequence;
}
}

extern "C" void event_queue_sample(unsigned lane, unsigned reset, unsigned valid,
    unsigned ready, unsigned payload, unsigned out_valid, unsigned out_ready, unsigned out_payload) {
  auto& state = lanes.at(lane);
  auto& covered = coverage.at(lane);
  in_reset = reset;
  if (reset) {
    if (!state.pending.empty()) ++covered.flushes;
    state = Lane{};
    expected.clear();
    cycle = 0;
    return;
  }
  const unsigned depth = lane < 4 ? 1 : 3;
  const bool flow = lane % 2 == 1 || lane >= 8;
  const bool pipe = lane % 4 >= 2 || lane >= 8;
  const bool composed = lane == 9;
  const bool empty = state.pending.empty(), full = state.pending.size() == depth;
  const bool input = valid && ready, output = out_valid && out_ready;
  if (!valid) ++covered.bubbles;
  if ((valid && !ready) || (out_valid && !out_ready)) ++covered.stalls;
  if (!composed) {
    if (full) ++covered.full;
    if (bool(ready) != (!full || (pipe && out_ready))) fail("queue ready mismatch");
    if (bool(out_valid) != (!empty || (flow && valid))) fail("queue valid mismatch");
    if (out_valid && out_payload != (empty ? payload : state.pending.front().payload))
      fail("queue offered payload mismatch");
    if (input && !(flow && empty && out_ready)) ++covered.stored;
    if (empty && input && output) ++covered.bypass;
    if (full && input && output) ++covered.full_replace;
  }
  if (input) {
    ++covered.accepted;
    const auto sequence = node(lane, 0, payload);
    if (!composed || (payload != 0 && payload != 3))
      state.pending.push_back({sequence, cycle, composed ? (payload + 1) & 255 : payload});
  }
  if (output) {
    if (state.pending.empty()) fail("queue output has no accepted parent");
    const auto item = state.pending.front();
    state.pending.pop_front();
    if (out_payload != item.payload || (composed && cycle < item.cycle + 2))
      fail("queue output payload/order/latency mismatch");
    node(lane, 1, out_payload, item.parent);
  }
}

extern "C" void event_queue_check() {
  if (rhodium_event::graph().json() != expected.json())
    fail("queue event graph mismatch at cycle " + std::to_string(cycle)
         + "\nactual: " + rhodium_event::graph().json() + "expected: " + expected.json());
  if (!in_reset) ++cycle;
}

extern "C" void event_queue_finish() {
  for (unsigned lane = 0; lane < lanes.size(); ++lane) {
    if (!lanes[lane].pending.empty()) fail("queue fixture failed to drain");
    const auto& c = coverage[lane];
    if (c.accepted < 50 || !c.stalls || !c.bubbles || !c.flushes)
      fail("queue fixture missed traffic/reset coverage on lane " + std::to_string(lane));
    if (lane == 9) continue;
    if (!c.full || c.stored < 30 || ((lane % 2 == 1 || lane == 8) && !c.bypass) ||
        ((lane % 4 >= 2 || lane == 8) && !c.full_replace))
      fail("queue fixture missed storage/wrap/bypass/full replacement coverage on lane " + std::to_string(lane));
  }
}
