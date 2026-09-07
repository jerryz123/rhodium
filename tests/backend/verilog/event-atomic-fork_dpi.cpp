// Reconstructs fork ancestry from public transfers and compares the complete DPI graph.
#include "../../../rhodium/event/runtime/rheg.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <deque>

namespace {
struct Pending { unsigned site, payload; std::uint64_t sequence; };
struct Lane {
  std::deque<Pending> input;
  std::array<std::deque<Pending>, 3> branches;
  std::array<std::uint64_t, 4> sequences{};
};
struct Coverage {
  std::array<unsigned, 3> outputs{}, stalls{};
  unsigned forks = 0, blocked = 0, bubbles = 0, flushes = 0, independent = 0, simultaneous = 0;
};
std::array<Lane, 3> lanes;
std::array<Coverage, 3> coverage;
rheg::Graph expected;
std::uint64_t cycle = 0;
bool in_reset = true;
[[noreturn]] void fail(const std::string& message) {
  std::fprintf(stderr, "%s\n", message.c_str());
  std::abort();
}
Pending pop(std::deque<Pending>& queue) {
  if (queue.empty()) fail("fork transfer has no accepted ancestor");
  auto item = queue.front(); queue.pop_front(); return item;
}
Pending node(unsigned lane, unsigned local, unsigned payload) {
  const auto sequence = lanes[lane].sequences[local]++;
  const auto site = lane * 4 + local;
  auto& value = expected.nodes[{site, sequence}];
  value.present = true; value.width = 8; value.cycle = cycle; value.words[0] = payload;
  return {site, payload, sequence};
}
}
extern "C" void event_atomic_fork_sample(unsigned lane, unsigned reset, unsigned in_valid,
    unsigned in_ready, unsigned payload, unsigned fork_valid, unsigned fork_ready,
    unsigned transfers, unsigned out_valid, unsigned out_ready, unsigned payloads) {
  auto& state = lanes.at(lane); auto& c = coverage.at(lane);
  in_reset = reset;
  if (reset) {
    bool pending = !state.input.empty();
    for (const auto& queue : state.branches) pending |= !queue.empty();
    if (pending) ++c.flushes;
    state = Lane{}; expected.clear(); cycle = 0; return;
  }
  const bool replicated = fork_valid && fork_ready;
  if (transfers != (replicated ? 7u : 0u)) fail("fork recipients did not accept atomically");
  if (fork_valid && !fork_ready) ++c.blocked;
  if (!in_valid) ++c.bubbles;
  if (in_valid && in_ready) state.input.push_back(node(lane, 0, payload));
  if (replicated) {
    ++c.forks;
    auto parent = pop(state.input);
    for (auto& queue : state.branches) queue.push_back(parent);
  }
  const unsigned fired = out_valid & out_ready;
  if (lane == 0 && fired != transfers) fail("unbuffered fork completed non-atomically");
  if (fired && fired != 7) ++c.independent;
  if (fired == 7) ++c.simultaneous;
  for (unsigned branch = 0; branch < 3; ++branch) {
    if ((out_valid & ~out_ready) & (1u << branch)) ++c.stalls[branch];
    if (fired & (1u << branch)) {
      ++c.outputs[branch];
      auto parent = pop(state.branches[branch]);
      const auto out_payload = (payloads >> (8 * branch)) & 255;
      if (parent.payload != out_payload) fail("fork branch payload/order mismatch");
      auto child = node(lane, branch + 1, out_payload);
      expected.edges.insert({{parent.site, parent.sequence}, {child.site, child.sequence}});
    }
  }
}
extern "C" void event_atomic_fork_check() {
  if (rheg::graph().json() != expected.json())
    fail("atomic fork graph mismatch at cycle " + std::to_string(cycle)
         + "\nactual: " + rheg::graph().json() + "expected: " + expected.json());
  if (!in_reset) ++cycle;
}
extern "C" void event_atomic_fork_finish() {
  for (unsigned lane = 0; lane < lanes.size(); ++lane) {
    const auto& state = lanes[lane]; const auto& c = coverage[lane];
    if (!state.input.empty()) fail("fork fixture did not drain ingress");
    for (unsigned branch = 0; branch < 3; ++branch) {
      if (!state.branches[branch].empty()) fail("fork fixture did not drain branch");
      if (c.outputs[branch] < 25 || !c.stalls[branch]) fail("fork fixture missed branch coverage");
    }
    if (c.forks < 25 || !c.blocked || !c.bubbles || !c.simultaneous ||
        (lane > 0 && (!c.flushes || !c.independent)))
      fail("fork fixture missed required coverage on lane " + std::to_string(lane));
  }
}
