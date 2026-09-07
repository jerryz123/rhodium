// Tests manifest validation, callback ordering, stable snapshots, and reset epochs.
#include "../../rhodium/event/runtime/rhodium_event.h"
#include <algorithm>
#include <array>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <type_traits>

using namespace rhodium_event;
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

  // Real ABI: same-site distinct occurrences survive deduplication; large
  // sequence/cycle identities remain decimal strings in the bundled export.
  rhodium_event::graph().bind_manifest(descriptor());
  constexpr std::uint64_t large = 9007199254740993ULL;
  rhodium_event_edge(1, large, 0, large);
  rhodium_event_edge(1, large, 0, large);
  rhodium_event_edge(1, large, 0, large + 1);
  rhodium_event_payload(1, large, 0, 7);
  rhodium_event_node(1, large, large, 8);
  rhodium_event_node(0, large, large, 0);
  rhodium_event_node(0, large + 1, large, 0);
  const auto snapshot = rhodium_event::graph().snapshot();
  require(snapshot.edges().size() == 2);
  require(snapshot.json().find("\"9007199254740993\"") != std::string::npos);
  require(snapshot.json() == "{\"format\":\"rhodium-event-trace\",\"version\":1,\"manifest\":" +
          descriptor().json + ",\"occurrences\":" + rhodium_event::graph().json() + "}\n");
  rhodium_event_reset(1);
  require(rhodium_event::graph().snapshot().nodes().empty());
  require(snapshot.nodes().size() == 3);
  std::cout << snapshot.json();
  std::cerr << "event collector tests passed (120 callback permutations)\n";
}
