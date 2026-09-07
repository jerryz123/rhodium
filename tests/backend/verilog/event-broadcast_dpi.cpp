// Reconstructs broadcast ancestry from public transfers and compares the complete DPI graph.
#include "../../../rhodium/event/runtime/rheg.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <deque>

namespace {
struct Pending { unsigned site, payload; std::uint64_t sequence; };
struct Lane {
  std::deque<Pending> input;
  Pending resident{};
  unsigned pending = 0;
  std::array<std::deque<Pending>, 3> branches;
  std::array<std::uint64_t, 4> sequences{};
};
struct Coverage {
  std::array<unsigned, 3> outputs{}, stalls{};
  unsigned partial_replacements = 0, buffered_flushes = 0, replacements = 0, partial_flushes = 0, acceptances = 0, blocked = 0, bubbles = 0, flushes = 0, independent = 0, simultaneous = 0;
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
  if (queue.empty()) fail("broadcast transfer has no accepted ancestor");
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
extern "C" void event_broadcast_sample(unsigned lane, unsigned reset, unsigned in_valid,
    unsigned in_ready, unsigned payload, unsigned broadcast_valid, unsigned broadcast_ready,
    unsigned transfers, unsigned out_valid, unsigned out_ready, unsigned payloads) {
  auto& state = lanes.at(lane); auto& c = coverage.at(lane);
  in_reset = reset;
  if (reset) {
    bool pending = !state.input.empty() || state.pending;
    if (state.pending && state.pending != 7) ++c.partial_flushes;
    for (const auto& queue : state.branches) {
      pending |= !queue.empty();
      if (!queue.empty()) ++c.buffered_flushes;
    }
    if (pending) ++c.flushes;
    state = Lane{}; expected.clear(); cycle = 0; return;
  }
  const bool replicated = broadcast_valid && broadcast_ready;

  if (broadcast_valid && !broadcast_ready) ++c.blocked;
  if (!in_valid) ++c.bubbles;
  if (in_valid && in_ready) state.input.push_back(node(lane, 0, payload));
  // Outputs consume the old resident before this edge can install a replacement.
  if (transfers & ~state.pending) fail("broadcast delivered a recipient twice or without acceptance");
  for (unsigned branch = 0; branch < 3; ++branch)
    if (transfers & (1u << branch)) state.branches[branch].push_back(state.resident);
  state.pending &= ~transfers;
  if (replicated) {
    if (state.pending) fail("broadcast overwrote an undelivered recipient");
    if (transfers) ++c.replacements;
    if (transfers && transfers != 7) ++c.partial_replacements;
    ++c.acceptances;
    state.resident = pop(state.input);
    state.pending = 7;
  }
  const unsigned fired = out_valid & out_ready;
  if (lane == 0 && fired != transfers) fail("plain broadcast transfer mismatch");
  if (fired && fired != 7) ++c.independent;
  if (fired == 7) ++c.simultaneous;
  for (unsigned branch = 0; branch < 3; ++branch) {
    if ((out_valid & ~out_ready) & (1u << branch)) ++c.stalls[branch];
    if (fired & (1u << branch)) {
      ++c.outputs[branch];
      auto parent = pop(state.branches[branch]);
      const auto out_payload = (payloads >> (8 * branch)) & 255;
      if (parent.payload != out_payload) fail("broadcast branch payload/order mismatch");
      auto child = node(lane, branch + 1, out_payload);
      expected.edges.insert({{parent.site, parent.sequence}, {child.site, child.sequence}});
    }
  }
}
extern "C" void event_broadcast_check() {
  if (rheg::graph().json() != expected.json())
    fail("buffered broadcast graph mismatch at cycle " + std::to_string(cycle)
         + "\nactual: " + rheg::graph().json() + "expected: " + expected.json());
  if (!in_reset) ++cycle;
}
extern "C" void event_broadcast_finish() {
  for (unsigned lane = 0; lane < lanes.size(); ++lane) {
    const auto& state = lanes[lane]; const auto& c = coverage[lane];
    if (!state.input.empty() || state.pending) fail("broadcast fixture did not drain ingress");
    for (unsigned branch = 0; branch < 3; ++branch) {
      if (!state.branches[branch].empty()) fail("broadcast fixture did not drain branch");
      if (c.outputs[branch] < 25 || !c.stalls[branch]) fail("broadcast fixture missed branch coverage");
    }
    if (c.acceptances < 25 || !c.blocked || !c.bubbles || !c.simultaneous ||
        !c.flushes || !c.independent || !c.replacements || (lane == 0 && (!c.partial_flushes || !c.partial_replacements)) || (lane > 0 && !c.buffered_flushes))
      fail("broadcast fixture missed required coverage on lane " + std::to_string(lane));
  }
}
