// Reconstructs arbiter ancestry from accepted transfers, never from compiler grant observations.
#include "../../../rhodium/event/runtime/rheg.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <deque>

namespace {
struct Pending { unsigned site, payload; std::uint64_t sequence; };
struct Lane {
  std::array<std::deque<Pending>, 3> inputs;
  std::deque<Pending> joined, output;
  std::array<std::uint64_t, 4> sequences{};
  bool stalled = false;
  unsigned offered = 0;
};
struct Coverage {
  std::array<unsigned, 3> accepted{};
  unsigned contention = 0, stalls = 0, offer_changes = 0, flushes = 0, outputs = 0;
};
std::array<Lane, 5> lanes;
std::array<Coverage, 5> coverage;
rheg::Graph expected;
std::uint64_t cycle = 0;
bool in_reset = true;

[[noreturn]] void fail(const std::string& message) {
  std::fprintf(stderr, "%s\n", message.c_str());
  std::abort();
}
Pending pop(std::deque<Pending>& queue) {
  if (queue.empty()) fail("arbiter transfer has no accepted ancestor");
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

extern "C" void event_arbiter_sample(unsigned lane, unsigned reset, unsigned valid,
    unsigned ready, unsigned payloads, unsigned taken, unsigned join_fire,
    unsigned out_valid, unsigned out_ready, unsigned out_payload) {
  auto& state = lanes.at(lane);
  auto& c = coverage.at(lane);
  in_reset = reset;
  if (reset) {
    if (!state.joined.empty() || !state.output.empty() || !state.inputs[0].empty() ||
        !state.inputs[1].empty() || !state.inputs[2].empty()) ++c.flushes;
    state = Lane{}; expected.clear(); cycle = 0; return;
  }
  if (valid && (valid & (valid - 1))) ++c.contention;
  if (out_valid && !out_ready) ++c.stalls;
  if (state.stalled && out_valid && !out_ready && out_payload != state.offered) ++c.offer_changes;
  state.stalled = out_valid && !out_ready; state.offered = out_payload;
  for (unsigned input = 0; input < 3; ++input) {
    if ((valid & ready) & (1u << input)) {
      ++c.accepted[input];
      state.inputs[input].push_back(node(lane, input, (payloads >> (8 * input)) & 255));
    }
  }
  if (lane != 4 && taken && (taken & (taken - 1))) fail("arbiter accepted multiple winners");
  if (lane == 4 && ((taken & 3) == 3 || (join_fire && (taken & 4)))) fail("nested arbiter accepted multiple winners");
  for (unsigned input = 0; input < 3; ++input) {
    if (taken & (1u << input)) {
      auto item = pop(state.inputs[input]);
      if (lane == 4 && input < 2) state.joined.push_back(item);
      else state.output.push_back(item);
    }
  }
  if (join_fire) {
    if (lane != 4) fail("unexpected intermediate arbitration transfer");
    state.output.push_back(pop(state.joined));
  }
  if (out_valid && out_ready) {
    ++c.outputs;
    auto parent = pop(state.output);
    if (parent.payload != out_payload) fail("arbiter payload/order mismatch");
    auto child = node(lane, 3, out_payload);
    expected.edges.insert({{parent.site, parent.sequence}, {child.site, child.sequence}});
  }
}

extern "C" void event_arbiter_check() {
  if (rheg::graph().json() != expected.json())
    fail("arbiter graph mismatch at cycle " + std::to_string(cycle)
         + "\nactual: " + rheg::graph().json() + "expected: " + expected.json());
  if (!in_reset) ++cycle;
}
extern "C" void event_arbiter_finish() {
  for (unsigned lane = 0; lane < lanes.size(); ++lane) {
    const auto& state = lanes[lane]; const auto& c = coverage[lane];
    if (!state.joined.empty() || !state.output.empty()) fail("arbiter fixture did not drain outputs");
    for (const auto& queue : state.inputs) if (!queue.empty()) fail("arbiter fixture did not drain inputs");
    for (unsigned input = 0; input < c.accepted.size(); ++input)
      if (c.accepted[input] < 8) fail("arbiter fixture starved lane " + std::to_string(lane)
          + " input " + std::to_string(input) + " accepted=" + std::to_string(c.accepted[input]));
    if (!c.contention || !c.stalls || c.outputs < 30 || (lane >= 2 && !c.flushes) || (lane < 2 && !c.offer_changes))
      fail("arbiter fixture missed required coverage on lane " + std::to_string(lane));
  }
}
