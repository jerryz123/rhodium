// Tests manifest validation, callback ordering, timed snapshots, and streaming boundaries.
#include "../runtime/rheg.h"
#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

using namespace rheg;
namespace {
void require(bool condition) {
  if (!condition) throw std::runtime_error("collector test expectation failed");
}
template<class F> void rejects(F action, const std::string& message) {
  try { action(); }
  catch (const std::runtime_error& error) {
    if (std::string(error.what()).find(message) != std::string::npos) return;
    throw;
  }
  throw std::runtime_error("collector accepted invalid input: " + message);
}
Manifest descriptor() {
  return {R"({"format":"rhodium-event-graph","version":1,"top":"Test","sites":[{"id":"a","payload_width":false},{"id":"b","payload_width":8}],"dependencies":[{"parent":"a","child":"b"}]})",
          {0, 8}, {{0, 1}}};
}
Graph populated() {
  Graph graph;
  graph.bind_manifest(descriptor());
  graph.record_node({0, 9}, 2, 0);
  graph.record_node({1, 4}, 2, 8);
  graph.record_payload({1, 4}, 0, 7);
  graph.record_edge({0, 9}, {1, 4});
  return graph;
}
}
int main() {
  static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<Snapshot>().nodes())>>);
  static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<Snapshot>().edges())>>);
  static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<Snapshot>().manifest())>>);
  static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<Snapshot>().timing())>>);
  require(!populated().snapshot().timing());
  const auto expected = populated().snapshot().json();
  // All callback permutations, including duplicate edges before either node.
  std::array<unsigned, 5> order{0, 1, 2, 3, 4};
  do {
    Graph graph;
    graph.bind_manifest(descriptor());
    for (auto op : order) {
      if (op == 0) graph.record_node({0, 9}, 2, 0);
      if (op == 1) graph.record_node({1, 4}, 2, 8);
      if (op == 2) graph.record_payload({1, 4}, 0, 7);
      if (op >= 3) graph.record_edge({0, 9}, {1, 4});
    }
    require(graph.snapshot().json() == expected);
  } while (std::next_permutation(order.begin(), order.end()));

  Graph unbound;
  rejects([&] { unbound.snapshot(); }, "bound compiler manifest");
  unbound.reset(false);
  rejects([&] { unbound.bind_manifest(descriptor()); }, "before callbacks");
  auto graph = populated();
  const auto saved = graph.snapshot();
  graph.reset(false);
  require(graph.snapshot().json() == expected);
  graph.reset(true);
  require(graph.snapshot().nodes().empty());
  require(saved.json() == expected);
  rejects([&] { graph.bind_manifest(descriptor()); }, "before callbacks");
  graph.record_edge({0, 9}, {1, 4});
  rejects([&] { graph.snapshot(); }, "no corresponding node");
  graph.record_node({0, 9}, 2, 0);
  graph.record_payload({1, 4}, 0, 7);
  rejects([&] { graph.snapshot(); }, "incomplete event node");
  graph.record_node({1, 4}, 2, 8);
  require(graph.snapshot().json() == expected);

  auto invalid = populated();
  invalid.record_node({2, 0}, 0, 0);
  rejects([&] { invalid.snapshot(); }, "unknown manifest site");
  invalid = populated(); invalid.nodes.at({1, 4}).width = 7;
  rejects([&] { invalid.snapshot(); }, "width differs");
  invalid = populated(); invalid.record_edge({1, 4}, {0, 9});
  rejects([&] { invalid.snapshot(); }, "not a permitted");
  invalid = populated(); invalid.nodes.at({0, 9}).cycle = 3;
  rejects([&] { invalid.snapshot(); }, "parent occurs after");
  invalid = populated(); invalid.nodes.at({1, 4}).words[0] = 256;
  rejects([&] { invalid.snapshot(); }, "padding");
  invalid = populated(); invalid.nodes.at({1, 4}).words.clear();
  rejects([&] { invalid.snapshot(); }, "incomplete");
  invalid = populated(); invalid.nodes.at({1, 4}).words = {{1, 7}};
  rejects([&] { invalid.snapshot(); }, "missing event payload word");
  invalid = populated();
  rejects([&] { invalid.record_node({0, 9}, 2, 0); }, "duplicate event identity");
  rejects([&] { invalid.record_payload({1, 4}, 0, 7); }, "duplicate event payload word");

  auto bad_descriptor = descriptor(); bad_descriptor.dependencies.insert({0, 2});
  Graph empty;
  rejects([&] { empty.bind_manifest(bad_descriptor); }, "unknown site");
  bad_descriptor = descriptor(); bad_descriptor.payload_widths.clear();
  rejects([&] { empty.bind_manifest(bad_descriptor); }, "invalid event manifest");
  auto owned = descriptor(); empty.bind_manifest(owned); owned.payload_widths.clear();
  require(empty.snapshot().manifest().payload_widths.size() == 2);

  Graph timed;
  rejects([&] { timed.bind_timing(TraceTiming{}); }, "frequency must be positive");
  rejects([&] { timed.bind_timing({0}); }, "frequency must be positive");
  TraceTiming timing{100000000, 17};
  timed.bind_timing(timing);
  timing.clock_frequency_hz = 1;
  timed.bind_manifest(descriptor());
  rejects([&] { timed.bind_timing({1}); }, "once before callbacks");
  timed.reset(true);
  timed.reset(true);
  require(timed.snapshot().timing()->epoch_id == 17);
  timed.reset(false);
  timed.record_node({0, 0}, 0, 0);
  const auto timed_saved = timed.snapshot();
  require(timed_saved.timing()->clock_frequency_hz == 100000000);
  require(timed_saved.json().find("\"timing\":{\"clock_frequency_hz\":\"100000000\",\"epoch_id\":\"17\",\"origin\":\"cycle-zero\"}") != std::string::npos);
  timed.reset(true);
  timed.reset(true);
  require(timed.snapshot().timing()->epoch_id == 18);
  require(timed.snapshot().nodes().empty());
  timed.reset(false);
  timed.reset(true); // Even an empty running epoch gets its own identity.
  require(timed.snapshot().timing()->epoch_id == 19);
  require(timed_saved.timing()->epoch_id == 17);
  require(timed_saved.nodes().size() == 1);
  timed.record_node({0, 0}, 0, 0); // Asserted-only RTL ABI, no reset(false).
  timed.reset(true);
  require(timed.snapshot().timing()->epoch_id == 20);
  timed.clear();
  require(timed.snapshot().timing()->epoch_id == 20);
  rejects([&] { unbound.bind_timing({1}); }, "before callbacks");
  rejects([&] { graph.bind_timing({1}); }, "before callbacks");
  for (unsigned op = 0; op < 3; ++op) {
    Graph late;
    if (op == 0) late.record_node({0, 0}, 0, 0);
    if (op == 1) late.record_payload({0, 0}, 0, 0);
    if (op == 2) late.record_edge({0, 0}, {1, 0});
    late.clear();
    rejects([&] { late.bind_timing({1}); }, "before callbacks");
  }
  Graph exhausted;
  exhausted.bind_manifest(descriptor());
  exhausted.bind_timing({1, std::numeric_limits<std::uint64_t>::max()});
  exhausted.reset(true); // Initial reset does not consume an epoch ID.
  exhausted.record_node({0, 0}, 0, 0);
  rejects([&] { exhausted.reset(true); }, "epoch identity exhausted");
  require(exhausted.snapshot().nodes().size() == 1); // No destructive wraparound.

  Graph stream;
  stream.bind_manifest(descriptor());
  rejects([&] { stream.begin_stream(); }, "bound timing");
  stream.bind_timing({100000000});
  require(stream.begin_stream().nodes().empty());
  rejects([&] { stream.begin_stream(); }, "no active stream");
  stream.reset(true);
  stream.record_node({0, 0}, 0, 0);
  rejects([&] { stream.end_stream(); }, "finish event cycle");
  rejects([&] { stream.reset(true); }, "end event stream");
  rejects([&] { stream.clear(); }, "end event stream");
  const auto first_batch = stream.finish_cycle(0);
  require(first_batch.nodes.size() == 1);
  require(first_batch.json().find("\"format\":\"rhodium-event-cycle\"") != std::string::npos);
  rejects([&] { stream.finish_cycle(0); }, "cycle must increase");
  rejects([&] { stream.record_node({0, 1}, 0, 0); }, "already streamed");
  rejects([&] { stream.record_payload({0, 0}, 0, 0); }, "already streamed");
  stream.record_edge({0, 0}, {1, 0});
  stream.record_payload({1, 0}, 0, 7);
  rejects([&] { stream.finish_cycle(1); }, "incomplete event node");
  stream.record_node({1, 0}, 2, 8);
  rejects([&] { stream.finish_cycle(1); }, "outside unfinished");
  const auto second_batch = stream.finish_cycle(2);
  require(second_batch.nodes.size() == 1 && second_batch.edges.size() == 1);
  require(stream.snapshot().nodes().size() == 2); // Capture mode keeps history.
  rejects([&] { stream.record_edge({0, 0}, {1, 0}); }, "already streamed");
  require(stream.finish_cycle(3).nodes.empty());
  stream.end_stream();
  rejects([&] { stream.finish_cycle(4); }, "no active");
  stream.reset(true);
  require(stream.begin_stream().timing()->epoch_id == 1);
  stream.end_stream();
  require(first_batch.nodes.size() == 1); // Owning deltas survive reset.

  // Real ABI: same-site distinct occurrences survive deduplication; large
  // sequence/cycle identities remain decimal strings in the bundled export.
  rheg::graph().bind_manifest(descriptor());
  constexpr std::uint64_t large = 9007199254740993ULL;
  rheg::graph().bind_timing({large, large});
  rheg_edge(1, large, 0, large);
  rheg_edge(1, large, 0, large);
  rheg_edge(1, large, 0, large + 1);
  rheg_payload(1, large, 0, 7);
  rheg_node(1, large, large, 8);
  rheg_node(0, large, large, 0);
  rheg_node(0, large + 1, large, 0);
  const auto snapshot = rheg::graph().snapshot();
  require(snapshot.edges().size() == 2);
  require(snapshot.json().find("\"9007199254740993\"") != std::string::npos);
  require(snapshot.json() == "{\"format\":\"rhodium-event-trace\",\"version\":1,\"manifest\":" +
          descriptor().json + ",\"timing\":{\"clock_frequency_hz\":\"9007199254740993\",\"epoch_id\":\"9007199254740993\",\"origin\":\"cycle-zero\"},\"occurrences\":" + rheg::graph().json() + "}\n");
  rheg_reset(1);
  require(rheg::graph().snapshot().nodes().empty());
  require(snapshot.nodes().size() == 3);
  require(snapshot.timing()->epoch_id == large);
  require(rheg::graph().snapshot().timing()->epoch_id == large + 1);
  std::cout << snapshot.json();
  std::cerr << "event collector tests passed (120 callback permutations)\n";
}
