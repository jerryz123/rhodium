// Checks manifest-bound join snapshots against parent sets reconstructed from public transfers.
#include "../../../rhodium/event/runtime/rheg.h"
#include "event-join_manifest.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <vector>

namespace {
struct Token { unsigned payload = 0; std::vector<rheg::Ref> parents; };
struct Lane {
  std::array<std::deque<Token>, 3> inputs;
  std::array<std::deque<Token>, 2> replicas, outputs;
  std::deque<Token> rejoined, combined;
  Token resident;
  unsigned pending = 0;
  std::array<std::uint64_t, 6> sequences{};
};
struct Coverage {
  unsigned first = 0, rejoined = 0, pair_selected = 0, single_selected = 0;
  unsigned flushes = 0, replacements = 0, independent = 0, deduplicated = 0;
  std::array<unsigned, 2> outputs{}, stalls{};
};
std::array<Lane, 4> lanes;
std::array<Coverage, 4> coverage;
constexpr std::array<unsigned, 4> bases{0, 5, 10, 16};
rheg::Graph expected;
std::uint64_t cycle = 0;
bool in_reset = true;
std::array<std::deque<Token>, 2> occurrences;
std::array<std::uint64_t, 2> occurrence_sequences{};
unsigned distinct_occurrence_pairs = 0;
[[noreturn]] void fail(const std::string& message) {
  std::fprintf(stderr, "%s\n", message.c_str()); std::abort();
}
Token pop(std::deque<Token>& queue) {
  if (queue.empty()) fail("join boundary transfer has no accepted input");
  auto item = queue.front(); queue.pop_front(); return item;
}
Token node(unsigned lane, unsigned local, unsigned payload, const std::vector<rheg::Ref>& parents = {}) {
  const rheg::Ref self{bases[lane] + local, lanes[lane].sequences[local]++};
  auto& value = expected.nodes[self];
  value.present = true; value.width = 8; value.cycle = cycle; value.words[0] = payload;
  for (const auto& parent : parents) expected.edges.insert({parent, self});
  return {payload, {self}};
}
Token join(Token left, const Token& right, bool xor_payload) {
  if (xor_payload) left.payload ^= right.payload;
  left.parents.insert(left.parents.end(), right.parents.begin(), right.parents.end());
  return left;
}
bool pending(const Lane& state) {
  if (state.pending || !state.rejoined.empty() || !state.combined.empty()) return true;
  for (const auto& queue : state.inputs) if (!queue.empty()) return true;
  for (const auto& queue : state.replicas) if (!queue.empty()) return true;
  for (const auto& queue : state.outputs) if (!queue.empty()) return true;
  return false;
}
}
extern "C" void event_join_sample(unsigned lane, unsigned reset, unsigned in_valid,
    unsigned in_ready, unsigned payloads, unsigned first_fire, unsigned replica_fire,
    unsigned rejoin_fire, unsigned combine_inputs, unsigned combine_fire,
    unsigned route_fire, unsigned out_valid, unsigned out_ready, unsigned out_payloads) {
  auto& state = lanes.at(lane); auto& c = coverage.at(lane);
  in_reset = reset;
  if (reset) {
    if (pending(state)) ++c.flushes;
    state = Lane{}; expected.clear(); cycle = 0; return;
  }
  for (unsigned input = 0; input < 3; ++input)
    if ((in_valid & in_ready) & (1u << input))
      state.inputs[input].push_back(node(lane, input, (payloads >> (8 * input)) & 255));
  // A buffered broadcast's old deliveries precede replacement capture.
  if (lane != 0) {
    if (replica_fire & ~state.pending) fail("duplicate or invented replica delivery");
    for (unsigned output = 0; output < 2; ++output)
      if (replica_fire & (1u << output)) state.replicas[output].push_back(state.resident);
    state.pending &= ~replica_fire;
    if (replica_fire && replica_fire != 3) ++c.independent;
  } else if (replica_fire != (first_fire ? 3u : 0u)) fail("atomic fork did not transfer together");
  if (first_fire) {
    ++c.first;
    auto item = join(pop(state.inputs[0]), pop(state.inputs[1]), true);
    if (lane == 2) item = node(lane, 3, item.payload, item.parents);
    if (lane == 0) {
      for (auto& queue : state.replicas) queue.push_back(item);
    } else {
      if (state.pending) fail("broadcast overwrote pending lineage");
      if (replica_fire) ++c.replacements;
      state.resident = item; state.pending = 3;
    }
  }
  if (rejoin_fire) {
    ++c.rejoined;
    auto left = pop(state.replicas[0]); auto right = pop(state.replicas[1]);
    if (left.payload != right.payload) fail("replicas rejoined out of order");
    state.rejoined.push_back(join(left, right, false));
  }
  if (lane == 0) {
    if (combine_inputs != (combine_fire ? 3u : 0u)) fail("nested join consumed only part of its inputs");
    if (combine_fire) state.combined.push_back(join(pop(state.rejoined), pop(state.inputs[2]), true));
  } else {
    if (combine_inputs == 3 || bool(combine_inputs) != bool(combine_fire)) fail("arbiter selection mismatch");
    if (combine_inputs == 1) { ++c.pair_selected; state.combined.push_back(pop(state.rejoined)); }
    if (combine_inputs == 2) { ++c.single_selected; state.combined.push_back(pop(state.inputs[2])); }
  }
  if (route_fire) {
    if (route_fire == 3) fail("demux delivered to both outputs");
    auto item = pop(state.combined);
    const unsigned output = item.payload & 1;
    if (route_fire != (1u << output)) fail("demux payload selection mismatch");
    state.outputs[output].push_back(item);
  }
  for (unsigned output = 0; output < 2; ++output) {
    const unsigned mask = 1u << output;
    if (out_valid & ~out_ready & mask) ++c.stalls[output];
    if (out_valid & out_ready & mask) {
      ++c.outputs[output];
      const auto item = pop(state.outputs[output]);
      const auto payload = (out_payloads >> (8 * output)) & 255;
      if (payload != item.payload) fail("joined payload or ordering mismatch");
      const auto before = expected.edges.size();
      node(lane, (lane == 2 ? 4 : 3) + output, payload, item.parents);
      if (expected.edges.size() - before < item.parents.size()) ++c.deduplicated;
    }
  }
}
extern "C" void event_join_check() {
  if (rheg::graph().json() != expected.json())
    fail("join graph mismatch at cycle " + std::to_string(cycle)
         + "\nactual: " + rheg::graph().json() + "expected: " + expected.json());
  if (!in_reset) ++cycle;
}
extern "C" void event_join_occurrences(unsigned reset, unsigned input_fire, unsigned payload,
    unsigned output_fire, unsigned result) {
  if (reset) { occurrences = {}; occurrence_sequences = {}; return; }
  if (input_fire) {
    const rheg::Ref self{21, occurrence_sequences[0]++};
    auto& value = expected.nodes[self];
    value.present = true; value.width = 8; value.cycle = cycle; value.words[0] = payload;
    occurrences[payload & 1].push_back({payload, {self}});
  }
  if (output_fire) {
    auto item = join(pop(occurrences[0]), pop(occurrences[1]), true);
    if (item.payload != result) fail("distinct occurrence join payload mismatch");
    const rheg::Ref self{22, occurrence_sequences[1]++};
    auto& value = expected.nodes[self];
    value.present = true; value.width = 8; value.cycle = cycle; value.words[0] = result;
    const auto before = expected.edges.size();
    for (const auto& parent : item.parents) expected.edges.insert({parent, self});
    if (expected.edges.size() - before != 2) fail("distinct occurrences collapsed by site");
    ++distinct_occurrence_pairs;
  }
}
extern "C" void event_join_bind() {
  rheg::graph().bind_manifest(rheg_generated::manifest());
}
extern "C" void event_join_finish() {
  const auto snapshot = rheg::graph().snapshot();
  if (snapshot.nodes().size() != expected.nodes.size() || snapshot.edges().size() != expected.edges.size() ||
      snapshot.json().find("\"format\":\"rhodium-event-trace\"") == std::string::npos)
    fail("manifest-bound join snapshot differs from transfer scoreboard");
  if (!occurrences[0].empty() || !occurrences[1].empty() || occurrence_sequences[0] != 60 ||
      occurrence_sequences[1] != 30 || distinct_occurrence_pairs < 30)
    fail("distinct occurrence join did not drain or reach coverage");
  for (unsigned lane = 0; lane < lanes.size(); ++lane) {
    const auto& state = lanes[lane]; const auto& c = coverage[lane];
    if (pending(state)) fail("join fixture failed to drain lane " + std::to_string(lane));
    for (unsigned input = 0; input < 3; ++input)
      if (state.sequences[input] != 60) fail("join fixture missed final input quota");
    if (c.first < 50 || c.rejoined < 50 || !c.flushes || !c.deduplicated ||
        (lane && (!c.pair_selected || !c.single_selected || !c.replacements || !c.independent)))
      fail("join fixture missed lineage coverage on lane " + std::to_string(lane));
    for (unsigned output = 0; output < 2; ++output)
      if (c.outputs[output] < 20 || !c.stalls[output]) fail("join fixture missed output coverage");
  }
}
