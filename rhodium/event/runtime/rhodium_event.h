// Defines manifest-bound event snapshots, graph storage, and the fixed-width DPI ABI.
#ifndef RHODIUM_EVENT_H
#define RHODIUM_EVENT_H

#include <cstdint>
#include <map>
#include <memory>
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
// Trusted compiler output, not a runtime JSON parsing API. Site indices and
// allowed edges are generated together with the accompanying manifest JSON.
struct Manifest {
  std::string json;
  std::vector<std::uint32_t> payload_widths;
  std::set<std::pair<std::uint32_t, std::uint32_t>> dependencies; // parent, child
};
class Snapshot;
struct Graph {
  std::map<Ref, Node> nodes;
  std::set<std::pair<Ref, Ref>> edges; // parent, child
  void clear();
  void validate() const;
  std::string json() const;
  void bind_manifest(const Manifest& manifest);
  Snapshot snapshot() const;
  void record_node(Ref ref, std::uint64_t cycle, std::uint32_t width);
  void record_payload(Ref ref, std::uint32_t index, std::uint32_t word);
  void record_edge(Ref parent, Ref child);
  void reset(bool active);
private:
  friend class Snapshot;
  std::shared_ptr<const Manifest> manifest_;
  bool started_ = false;
};
// Owns a validated copy: later callbacks and reset cannot change this view.
class Snapshot {
public:
  const std::map<Ref, Node>& nodes() const { return graph_.nodes; }
  const std::set<std::pair<Ref, Ref>>& edges() const { return graph_.edges; }
  const Manifest& manifest() const { return *graph_.manifest_; }
  std::string json() const;
private:
  friend struct Graph;
  explicit Snapshot(const Graph& graph) : graph_(graph) {}
  Graph graph_;
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
