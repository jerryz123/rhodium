// Reconstructs exact demux ancestry from public routing transfers, not compiler observations.
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
  bool stalled = false;
  unsigned selector = 0;
};
struct Coverage {
  std::array<unsigned, 3> outputs{}, stalls{};
  unsigned invalid = 0, changes = 0, flushes = 0, simultaneous = 0, delayed = 0, bypass = 0;
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
  if (queue.empty()) fail("demux transfer has no accepted ancestor");
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
extern "C" void event_demux_sample(unsigned lane, unsigned reset, unsigned in_valid,
    unsigned in_ready, unsigned payload, unsigned selected, unsigned routing_valid,
    unsigned routing_ready, unsigned out_valid, unsigned out_ready, unsigned payloads) {
  auto& state = lanes.at(lane); auto& c = coverage.at(lane);
  in_reset = reset;
  if (reset) {
    bool pending = !state.input.empty();
    for (const auto& queue : state.branches) pending |= !queue.empty();
    if (pending) ++c.flushes;
    state = Lane{}; expected.clear(); cycle = 0; return;
  }
  if (routing_valid && selected == 3) {
    ++c.invalid;
    if (routing_ready) fail("invalid demux selector accepted an input");
  }
  if (state.stalled && routing_valid && state.selector != selected) ++c.changes;
  state.stalled = routing_valid && !routing_ready; state.selector = selected;
  if (in_valid && in_ready) state.input.push_back(node(lane, 0, payload));
  if (routing_valid && routing_ready) {
    if (selected >= 3) fail("out-of-range demux transfer");
    state.branches[selected].push_back(pop(state.input));
  }
  const unsigned fired = out_valid & out_ready;
  if (fired && (fired & (fired - 1))) ++c.simultaneous;
  for (unsigned branch = 0; branch < 3; ++branch) {
    if ((out_valid & ~out_ready) & (1u << branch)) ++c.stalls[branch];
    if (fired & (1u << branch)) {
      ++c.outputs[branch];
      auto parent = pop(state.branches[branch]);
      const auto out_payload = (payloads >> (8 * branch)) & 255;
      if (parent.payload != out_payload) fail("demux payload/order mismatch");
      if (branch != selected) ++c.delayed;
      if (lane > 0 && branch == 1 && routing_valid && routing_ready &&
          selected == branch && state.branches[branch].empty()) ++c.bypass;
      auto child = node(lane, branch + 1, out_payload);
      expected.edges.insert({{parent.site, parent.sequence}, {child.site, child.sequence}});
    }
  }
}
extern "C" void event_demux_check() {
  if (rheg::graph().json() != expected.json())
    fail("demux graph mismatch at cycle " + std::to_string(cycle)
         + "\nactual: " + rheg::graph().json() + "expected: " + expected.json());
  if (!in_reset) ++cycle;
}
extern "C" void event_demux_finish() {
  for (unsigned lane = 0; lane < lanes.size(); ++lane) {
    const auto& state = lanes[lane]; const auto& c = coverage[lane];
    if (!state.input.empty()) fail("demux fixture did not drain ingress");
    for (unsigned branch = 0; branch < 3; ++branch) {
      if (!state.branches[branch].empty()) fail("demux fixture did not drain branch");
      if (c.outputs[branch] < 8 || !c.stalls[branch]) fail("demux fixture missed branch coverage");
    }
    if (!c.invalid || !c.changes || (lane > 0 && (!c.flushes || !c.simultaneous || !c.delayed || !c.bypass)))
      fail("demux fixture missed required coverage on lane " + std::to_string(lane));
  }
}
