// Defines Rhodium Hardware Event Graph (rheg) snapshots, streaming batches, and DPI ABI.
#ifndef RHEG_H
#define RHEG_H

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace rheg {
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
// Run metadata, independent of circuit topology. Cycle zero means timestamp zero.
struct TraceTiming {
  std::uint64_t clock_frequency_hz = 0;
  std::uint64_t epoch_id = 0;
};
struct CycleBatch {
  std::uint64_t cycle;
  std::map<Ref, Node> nodes;
  std::set<std::pair<Ref, Ref>> edges;
  std::string json() const;
};
struct Graph {
  std::map<Ref, Node> nodes;
  std::set<std::pair<Ref, Ref>> edges; // parent, child
  void clear();
  void validate() const;
  std::string json() const;
  void bind_manifest(const Manifest& manifest);
  void bind_timing(const TraceTiming& timing);
  Snapshot snapshot() const;
  // Begin on an empty bound graph; finish only after callbacks have settled.
  Snapshot begin_stream();
  CycleBatch finish_cycle(std::uint64_t cycle);
  void end_stream();
  void record_node(Ref ref, std::uint64_t cycle, std::uint32_t width);
  void record_payload(Ref ref, std::uint32_t index, std::uint32_t word);
  void record_edge(Ref parent, Ref child);
  void reset(bool active);
private:
  friend class Snapshot;
  std::shared_ptr<const Manifest> manifest_;
  std::optional<TraceTiming> timing_;
  bool started_ = false;
  bool epoch_active_ = false;
  bool streaming_ = false;
  std::optional<std::uint64_t> finished_cycle_;
  std::set<Ref> pending_nodes_;
  std::set<std::pair<Ref, Ref>> pending_edges_;
};
// Owns a validated copy: later callbacks and reset cannot change this view.
class Snapshot {
public:
  const std::map<Ref, Node>& nodes() const { return graph_.nodes; }
  const std::set<std::pair<Ref, Ref>>& edges() const { return graph_.edges; }
  const Manifest& manifest() const { return *graph_.manifest_; }
  const std::optional<TraceTiming>& timing() const { return graph_.timing_; }
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
void rheg_reset(std::uint8_t active);
void rheg_node(std::uint32_t site, std::uint64_t sequence,
                        std::uint64_t cycle, std::uint32_t width);
void rheg_payload(std::uint32_t site, std::uint64_t sequence,
                           std::uint32_t index, std::uint32_t word);
void rheg_edge(std::uint32_t child, std::uint64_t child_sequence,
                        std::uint32_t parent, std::uint64_t parent_sequence);
}
#endif
