/* Defines event identities, captures, snapshots, streaming batches, and the rheg DPI ABI. */
// SPDX-License-Identifier: Apache-2.0
#ifndef RHEG_H
#define RHEG_H

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace rheg {
/* Identifies one occurrence by compiler site index and that site's sequence number. */
struct Ref {
  std::uint32_t site;
  std::uint64_t sequence;
  /* Orders identities deterministically by site, then sequence, not occurrence time. */
  bool operator<(const Ref& other) const {
    return std::tie(site, sequence) < std::tie(other.site, other.sequence);
  }
};
/* Accumulates callbacks for one occurrence; payload words are least-significant first.
   An end cycle marks a residency release, not a transfer's visual slice end. */
struct Node {
  bool present = false;
  std::uint64_t cycle = 0;
  std::uint32_t width = 0;
  std::map<std::uint32_t, std::uint32_t> words;
  bool ancestry_unknown = false;
  std::optional<std::uint64_t> end_cycle = {};
};
/* Describes a named field in the compact selected capture, not the functional RTL
   bundle; optional ISA, enum symbols, and label selection guide presentation. */
struct Field {
  std::string name;
  std::uint32_t width, offset;
  std::string encoding;
  std::string isa = {}, pc = {};
  std::vector<std::pair<std::uint64_t, std::string>> symbols = {};
  bool label = false;
  /* Compares the complete layout and display schema when checking manifest agreement. */
  bool operator==(const Field& other) const {
    return std::tie(name, width, offset, encoding, isa, pc, symbols, label) ==
           std::tie(other.name, other.width, other.offset, other.encoding, other.isa, other.pc, other.symbols, other.label);
  }
};
/* Holds an extracted field at its original width without truncating wide captures. */
struct FieldValue {
  std::uint32_t width;
  std::string encoding;
  std::vector<std::uint32_t> words;
  /* Formats every captured bit as width-preserving hexadecimal. */
  std::string hex() const;
  /* Formats arbitrary-width decimal, interpreting signed captures as two's complement. */
  std::string decimal() const;
  /* Returns a scalar for fields up to 64 bits; rejects wider fields. */
  std::uint64_t unsigned_value() const;
  /* Returns the sign-extended scalar for fields up to 64 bits. */
  std::int64_t signed_value() const;
};
/* Extracts a schema field across payload-word boundaries, masking its final word. */
FieldValue capture_field(const Node& node, const Field& field);
/* Names a compiler-declared instance scope whose runtime identity is bound per epoch. */
struct InstanceScope {
  std::string id, label;
  std::uint32_t width;
  /* Compares scope identity, display label, and runtime identity width. */
  bool operator==(const InstanceScope& other) const {
    return std::tie(id, label, width) == std::tie(other.id, other.label, other.width);
  }
};
/* Records an instance identity and the first cycle in which its registration applies. */
struct InstanceValue { std::uint64_t value, cycle; };
/* Owns trusted compiler JSON and matching typed tables; this is not a JSON parser API.
   Site indices and allowed parent-child edges accompany the generated callbacks. */
struct Manifest {
  std::string json;
  std::vector<std::uint32_t> payload_widths;
  std::set<std::pair<std::uint32_t, std::uint32_t>> dependencies; // parent, child
  // Empty outer table denotes a legacy manifest without named captures.
  std::vector<std::vector<Field>> fields = {};
  std::set<std::uint32_t> residency_sites = {};
  std::vector<InstanceScope> instances = {};
  std::vector<std::vector<std::uint32_t>> site_instances = {};
};
/* Checks complete capture layouts, encodings, enum symbols, and instance ancestry. */
void validate_capture_schema(const Manifest& manifest);
class Snapshot;
/* Carries run metadata independent of circuit topology; cycle zero is timestamp zero. */
struct TraceTiming {
  std::uint64_t clock_frequency_hz = 0;
  std::uint64_t epoch_id = 0;
};
/* Owns a settled stream delta through cycle, including late residency-end updates.
   Edges may refer to old parents retained by the collector and writer. */
struct CycleBatch {
  std::uint64_t cycle;
  std::map<Ref, Node> nodes;
  std::set<std::pair<Ref, Ref>> edges;
  std::map<Ref, std::uint64_t> ends = {};
  std::map<std::uint32_t, InstanceValue> instances = {};
  /* Serializes the delta with exact decimal-string cycles and sequences. */
  std::string json() const;
};
/* Collects unordered simulator callbacks, validating completeness at settled boundaries.
   Streaming retains the full epoch graph as well as pending deltas. */
struct Graph {
  std::map<Ref, Node> nodes;
  std::set<std::pair<Ref, Ref>> edges; // parent, child
  /* Clears occurrences outside streaming, retaining bindings and instance registrations. */
  void clear();
  /* Checks complete payloads, permitted edges, cycle ordering, and instance registration. */
  void validate() const;
  /* Validates and serializes occurrences without the manifest or timing envelope. */
  std::string json() const;
  /* Copies and validates compiler metadata once, before any callbacks. */
  void bind_manifest(const Manifest& manifest);
  /* Binds positive-frequency timing once, before any callbacks. */
  void bind_timing(const TraceTiming& timing);
  /* Returns an independently owned, validated view with its bound manifest. */
  Snapshot snapshot() const;
  /* Starts an empty, manifest- and timing-bound stream and returns its header snapshot. */
  Snapshot begin_stream();
  /* Validates settled callbacks through a strictly increasing watermark, then drains
     pending deltas; failed validation leaves them available for completion and retry. */
  CycleBatch finish_cycle(std::uint64_t cycle);
  /* Ends streaming only after all pending callbacks have been drained. */
  void end_stream();
  /* Supplies occurrence metadata once; payload or edge callbacks may arrive first. */
  void record_node(Ref ref, std::uint64_t cycle, std::uint32_t width);
  /* Supplies one indexed payload word, rejecting duplicates and already-streamed nodes. */
  void record_payload(Ref ref, std::uint32_t index, std::uint32_t word);
  /* Marks unavailable ancestry without fabricating a parent reference. */
  void record_unknown(Ref ref);
  /* Records one residency release, including releases of previously streamed owners. */
  void record_end(Ref ref, std::uint64_t cycle);
  /* Adds an exact parent-child edge; streamed parents may acquire new children. */
  void record_edge(Ref parent, Ref child);
  /* Binds a declared scope once per epoch with a width-checked runtime identity. */
  void record_instance(std::uint32_t scope, std::uint64_t value, std::uint64_t cycle);
  /* Looks up and extracts a named field using the bound compiler schema. */
  FieldValue field(Ref ref, const std::string& name) const;
  /* Clears epoch activity on asserted reset, preserving bindings; repeated held reset
     does not repeatedly advance the epoch. End an active stream before resetting. */
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
  std::map<Ref, std::uint64_t> pending_ends_;
  std::map<std::uint32_t, InstanceValue> instances_;
  std::map<std::uint32_t, InstanceValue> pending_instances_;
};
/* Owns a validated copy: later collector callbacks and reset cannot change this view. */
class Snapshot {
public:
  /* Exposes immutable occurrences, including captured words and residency ends. */
  const std::map<Ref, Node>& nodes() const { return graph_.nodes; }
  /* Exposes immutable exact parent-child references. */
  const std::set<std::pair<Ref, Ref>>& edges() const { return graph_.edges; }
  /* Returns the compiler descriptor retained with this snapshot. */
  const Manifest& manifest() const { return *graph_.manifest_; }
  /* Returns optional run timing; untimed snapshots remain valid graph captures. */
  const std::optional<TraceTiming>& timing() const { return graph_.timing_; }
  /* Returns this epoch's registered runtime instance identities. */
  const std::map<std::uint32_t, InstanceValue>& instances() const { return graph_.instances_; }
  /* Serializes the versioned trace envelope with manifest, occurrences, and timing. */
  std::string json() const;
  /* Extracts a named capture without consulting the live collector. */
  FieldValue field(Ref ref, const std::string& name) const { return graph_.field(ref, name); }
private:
  friend struct Graph;
  /* Copies a graph only through the collector's validated snapshot path. */
  explicit Snapshot(const Graph& graph) : graph_(graph) {}
  Graph graph_;
};
/* Returns the collector for one instrumented top per process. Callbacks and queries
   execute on the simulator thread; site IDs index the compiler manifest array. */
Graph& graph();
}

extern "C" {
/* Forwards the sampled reset level to the process collector's epoch lifecycle. */
void rheg_reset(std::uint8_t active);
/* Registers a runtime instance identity under its compiler-assigned scope index. */
void rheg_instance(std::uint32_t scope, std::uint64_t value, std::uint64_t cycle);
/* Marks an occurrence whose ancestry could not be recovered by instrumentation. */
void rheg_unknown(std::uint32_t site, std::uint64_t sequence);
/* Reports the release cycle of a captured residency owner. */
void rheg_end(std::uint32_t site, std::uint64_t sequence, std::uint64_t cycle);
/* Reports a site occurrence's cycle and compact capture width. */
void rheg_node(std::uint32_t site, std::uint64_t sequence,
                        std::uint64_t cycle, std::uint32_t width);
/* Reports one least-significant-word-first payload chunk for an occurrence. */
void rheg_payload(std::uint32_t site, std::uint64_t sequence,
                           std::uint32_t index, std::uint32_t word);
/* Accepts child-first DPI arguments and records the corresponding parent-child edge. */
void rheg_edge(std::uint32_t child, std::uint64_t child_sequence,
                        std::uint32_t parent, std::uint64_t parent_sequence);
}
#endif
