// Checks simulated event occurrences against an independent transfer scoreboard.
#include "../../../rhodium/event/runtime/rheg.h"
#include <stdexcept>

namespace {
using rheg::Ref;
rheg::Graph expected;
void expect(std::uint32_t site, std::uint64_t seq, std::uint64_t cycle,
            std::uint64_t payload, std::uint32_t high = 0,
            bool wide = false, int parent = -1, std::uint64_t parent_seq = 0) {
  auto& node = expected.nodes[{site, seq}];
  node.present = true;
  node.cycle = cycle;
  node.width = wide ? 65 : 8;
  node.words[0] = static_cast<std::uint32_t>(payload);
  if (wide) {
    node.words[1] = static_cast<std::uint32_t>(payload >> 32);
    node.words[2] = high;
  }
  if (parent >= 0) expected.edges.insert({{static_cast<std::uint32_t>(parent), parent_seq}, {site, seq}});
}
void full(std::uint64_t left, std::uint64_t right, std::uint64_t wide, std::uint32_t high) {
  expect(0, 0, 0, left);
  expect(6, 0, 0, left + 1, 0, false, 0, 0);
  expect(1, 0, 0, left + 1, 0, false, 6, 0);
  expect(2, 0, 0, right);
  expect(7, 0, 0, right + 1, 0, false, 2, 0);
  expect(3, 0, 0, right + 1, 0, false, 7, 0);
  expect(4, 0, 0, wide, high, true);
  expect(5, 0, 0, wide, high, true, 4, 0);
}
}

extern "C" void event_runtime_check(std::uint32_t phase) {
  if (phase == 0 || phase == 5) expected.clear();
  if (phase == 1) full(10, 20, 0x0123456789abcdefULL, 1);
  if (phase == 3) {
    expect(2, 1, 2, 2);
    expect(7, 1, 2, 3, 0, false, 2, 1);
    expect(4, 1, 2, 0, 0, true);
  }
  if (phase == 4) {
    expect(0, 1, 3, 2);
    expect(6, 1, 3, 3, 0, false, 0, 1);
    expect(4, 2, 3, 9, 1, true);
    expect(5, 1, 3, 9, 1, true, 4, 2);
  }
  if (phase == 6) full(7, 8, 1, 0);
  if (rheg::graph().json() != expected.json())
    throw std::runtime_error("event graph differs from transfer scoreboard at phase " + std::to_string(phase)
                             + "\nactual: " + rheg::graph().json() + "expected: " + expected.json());
}

extern "C" void event_runtime_order_test() {
  // Edges and payload chunks may arrive before their node callbacks.
  rheg_edge(1, 4, 0, 9);
  rheg_edge(1, 4, 0, 9);
  rheg_payload(1, 4, 0, 7);
  rheg_node(1, 4, 2, 8);
  rheg_node(0, 9, 2, 0);
  rheg::graph().validate();
  if (rheg::graph().edges.size() != 1) throw std::runtime_error("edge deduplication failed");
  bool rejected = false;
  try { rheg_node(0, 9, 2, 0); } catch (const std::runtime_error&) { rejected = true; }
  if (!rejected) throw std::runtime_error("duplicate identity was accepted");
  rheg_reset(1);
}
