// Uses transaction FIFOs, not pipeline enables, to check elastic runtime parent identity.
#include "../../../rhodium/event/runtime/rheg.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <deque>

namespace {
struct Pending { std::uint64_t parent, cycle; std::uint32_t payload; };
struct Lane {
  std::deque<Pending> middle, completed;
  std::array<std::uint64_t, 3> sequences{};
};
std::array<Lane, 2> lanes;
rheg::Graph expected;
std::uint64_t cycle = 0, stalls = 0, simultaneous = 0, flushes = 0, bubbles = 0;
std::array<unsigned, 2> accepted{};
bool in_reset = true;

[[noreturn]] void fail(const std::string& message) {
  std::fprintf(stderr, "%s\n", message.c_str());
  std::abort();
}

std::uint64_t node(unsigned lane, unsigned local_site, unsigned payload,
                   int parent = -1, std::uint64_t parent_sequence = 0) {
  const auto sequence = lanes[lane].sequences[local_site]++;
  const auto site = lane * 3 + local_site;
  auto& value = expected.nodes[{site, sequence}];
  value.present = true;
  value.width = 8;
  value.cycle = cycle;
  value.words[0] = payload;
  if (parent >= 0)
    expected.edges.insert({{lane * 3 + static_cast<unsigned>(parent), parent_sequence}, {site, sequence}});
  return sequence;
}
}

extern "C" void event_elastic_sample(unsigned lane, unsigned reset, unsigned valid,
    unsigned ready, unsigned payload, unsigned middle_fire, unsigned middle_payload,
    unsigned out_valid, unsigned out_ready, unsigned out_payload) {
  auto& state = lanes.at(lane);
  in_reset = reset;
  if (reset) {
    if (!state.middle.empty() || !state.completed.empty()) ++flushes;
    state = Lane{};
    expected.clear();
    cycle = 0;
    return;
  }
  const bool input = valid && ready, output = out_valid && out_ready;
  if ((valid && !ready) || (out_valid && !out_ready)) ++stalls;
  if (!valid) ++bubbles;
  if (input && middle_fire && output) ++simultaneous;
  if (input) {
    ++accepted[lane];
    const auto sequence = node(lane, 0, payload);
    if (payload != 0) state.middle.push_back({sequence, cycle, payload});
  }
  if (middle_fire) {
    if (state.middle.empty()) fail("elastic middle has no accepted parent");
    const auto item = state.middle.front();
    state.middle.pop_front();
    if (middle_payload != item.payload || cycle < item.cycle + 3)
      fail("elastic middle payload/order/minimum latency mismatch");
    const auto sequence = node(lane, 2, middle_payload, 0, item.parent);
    const auto mapped = (middle_payload + 1) & 255;
    if (mapped != 4) state.completed.push_back({sequence, cycle, mapped});
  }
  if (output) {
    if (state.completed.empty()) fail("elastic completion has no middle parent");
    const auto item = state.completed.front();
    state.completed.pop_front();
    if (out_payload != item.payload || cycle < item.cycle + 1)
      fail("elastic output payload/order/minimum latency mismatch");
    node(lane, 1, out_payload, 2, item.parent);
  }
}

extern "C" void event_elastic_check() {
  if (rheg::graph().json() != expected.json())
    fail("elastic event graph mismatch at cycle " + std::to_string(cycle)
         + "\nactual: " + rheg::graph().json() + "expected: " + expected.json());
  if (!in_reset) ++cycle;
}

extern "C" void event_elastic_finish() {
  for (const auto& lane : lanes)
    if (!lane.middle.empty() || !lane.completed.empty()) fail("elastic fixture failed to drain");
  if (!stalls || !simultaneous || !flushes || !bubbles || accepted[0] < 30 || accepted[1] < 30)
    fail("elastic fixture missed required transfer coverage: accepted=" + std::to_string(accepted[0])
         + "," + std::to_string(accepted[1]) + " stalls=" + std::to_string(stalls)
         + " simultaneous=" + std::to_string(simultaneous) + " flushes=" + std::to_string(flushes)
         + " bubbles=" + std::to_string(bubbles));
}
