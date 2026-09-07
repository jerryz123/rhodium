// Defines the order-independent in-memory event graph and fixed-width DPI ABI.
#ifndef RHODIUM_EVENT_H
#define RHODIUM_EVENT_H

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace rhodium_event {
struct Ref {
  std::uint32_t site;
  std::uint64_t sequence;
  bool operator<(const Ref& other) const {
    return std::tie(site, sequence) < std::tie(other.site, other.sequence);
  }
};
struct Node {
  bool present = false;
  std::uint64_t cycle = 0;
  std::uint32_t width = 0;
  std::map<std::uint32_t, std::uint32_t> words;
};
struct Graph {
  std::map<Ref, Node> nodes;
  std::set<std::pair<Ref, Ref>> edges; // parent, child
  void clear();
  void validate() const;
  std::string json() const;
};
// One instrumented simulation top per process. Callbacks and queries execute
// on the simulator thread. Site IDs index the compiler manifest's site array.
Graph& graph();
}

extern "C" {
void rhodium_event_reset(std::uint8_t active);
void rhodium_event_node(std::uint32_t site, std::uint64_t sequence,
                        std::uint64_t cycle, std::uint32_t width);
void rhodium_event_payload(std::uint32_t site, std::uint64_t sequence,
                           std::uint32_t index, std::uint32_t word);
void rhodium_event_edge(std::uint32_t child, std::uint64_t child_sequence,
                        std::uint32_t parent, std::uint64_t parent_sequence);
}
#endif
