// Collects DPI callbacks and exports validated snapshots with optional epoch timing.
#include "rhodium_event.h"

#include <sstream>
#include <stdexcept>
#include <limits>

namespace rhodium_event {
Graph& graph() { static Graph value; return value; }
void Graph::clear() { nodes.clear(); edges.clear(); }
void Graph::bind_timing(const TraceTiming& timing) {
  if (timing_ || started_ || !nodes.empty() || !edges.empty())
    throw std::runtime_error("event timing must be bound once before callbacks");
  if (!timing.clock_frequency_hz)
    throw std::runtime_error("event clock frequency must be positive");
  timing_ = timing;
}
void Graph::bind_manifest(const Manifest& manifest) {
  if (manifest_ || started_ || !nodes.empty() || !edges.empty())
    throw std::runtime_error("event manifest must be bound once before callbacks");
  if (manifest.json.empty() || manifest.payload_widths.empty() ||
      manifest.payload_widths.size() > std::numeric_limits<std::uint32_t>::max())
    throw std::runtime_error("invalid event manifest descriptor");
  for (const auto& edge : manifest.dependencies)
    if (edge.first >= manifest.payload_widths.size() || edge.second >= manifest.payload_widths.size())
      throw std::runtime_error("event manifest dependency has unknown site");
  manifest_ = std::make_shared<const Manifest>(manifest);
}
void Graph::validate() const {
  for (const auto& entry : nodes) {
    const auto& node = entry.second;
    if (manifest_) {
      if (entry.first.site >= manifest_->payload_widths.size())
        throw std::runtime_error("event node has unknown manifest site " + std::to_string(entry.first.site));
      if (node.present && node.width != manifest_->payload_widths[entry.first.site])
        throw std::runtime_error("event payload width differs from manifest at site " + std::to_string(entry.first.site));
    }
    if (!node.present || node.words.size() != (std::uint64_t(node.width) + 31) / 32)
      throw std::runtime_error("incomplete event node or payload");
    for (std::uint32_t i = 0; i < node.words.size(); ++i)
      if (!node.words.count(i)) throw std::runtime_error("missing event payload word");
    if (node.width % 32 && (node.words.rbegin()->second >> (node.width % 32)))
      throw std::runtime_error("nonzero event payload padding");
  }
  for (const auto& edge : edges) {
    if (!nodes.count(edge.first) || !nodes.count(edge.second))
      throw std::runtime_error("event edge has no corresponding node");
    if (manifest_ && !manifest_->dependencies.count({edge.first.site, edge.second.site}))
      throw std::runtime_error("event edge is not a permitted manifest dependency: " +
                               std::to_string(edge.first.site) + " -> " + std::to_string(edge.second.site));
    if (nodes.at(edge.first).cycle > nodes.at(edge.second).cycle)
      throw std::runtime_error("event parent occurs after child");
  }
}
Snapshot Graph::snapshot() const {
  if (!manifest_) throw std::runtime_error("event snapshot requires a bound compiler manifest");
  validate();
  return Snapshot(*this);
}
std::string Snapshot::json() const {
  std::string metadata;
  if (timing()) {
    metadata = ",\"timing\":{\"clock_frequency_hz\":\"" +
               std::to_string(timing()->clock_frequency_hz) + "\",\"epoch_id\":\"" +
               std::to_string(timing()->epoch_id) +
               "\",\"origin\":\"cycle-zero\"}";
  }
  return "{\"format\":\"rhodium-event-trace\",\"version\":1,\"manifest\":" +
         manifest().json + metadata + ",\"occurrences\":" + graph_.json() + "}\n";
}
std::string Graph::json() const {
  validate();
  std::ostringstream out;
  // Sequences and cycles are decimal strings: JS consumers must not round
  // 64-bit identities to floating-point numbers.
  out << "{\"format\":\"rhodium-event-occurrences\",\"version\":1,\"nodes\":[";
  bool comma = false;
  for (const auto& entry : nodes) {
    if (comma) out << ',';
    comma = true;
    out << "{\"site\":" << entry.first.site << ",\"sequence\":\"" << entry.first.sequence
        << "\",\"cycle\":\"" << entry.second.cycle << "\",\"width\":" << entry.second.width << ",\"words\":[";
    bool word_comma = false;
    for (const auto& word : entry.second.words) {
      if (word_comma) out << ',';
      word_comma = true;
      out << word.second;
    }
    out << "]}";
  }
  out << "],\"edges\":[";
  comma = false;
  for (const auto& edge : edges) {
    if (comma) out << ',';
    comma = true;
    out << "{\"parent\":[" << edge.first.site << ",\"" << edge.first.sequence
        << "\"],\"child\":[" << edge.second.site << ",\"" << edge.second.sequence << "\"]}";
  }
  out << "]}\n";
  return out.str();
}
void Graph::record_node(Ref ref, std::uint64_t cycle, std::uint32_t width) {
  started_ = true;
  epoch_active_ = true;
  auto& node = nodes[ref];
  if (node.present) throw std::runtime_error("duplicate event identity");
  node.present = true;
  node.cycle = cycle;
  node.width = width;
}
void Graph::record_payload(Ref ref, std::uint32_t index, std::uint32_t word) {
  started_ = true;
  epoch_active_ = true;
  auto& words = nodes[ref].words;
  if (!words.emplace(index, word).second) throw std::runtime_error("duplicate event payload word");
}
void Graph::record_edge(Ref parent, Ref child) {
  started_ = true;
  epoch_active_ = true;
  edges.insert({parent, child});
}
void Graph::reset(bool active) {
  started_ = true;
  if (active) {
    if (epoch_active_ && timing_) {
      if (timing_->epoch_id == std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("event epoch identity exhausted");
      ++timing_->epoch_id;
    }
    clear();
    epoch_active_ = false;
  } else {
    epoch_active_ = true;
  }
}
}

extern "C" void rhodium_event_reset(std::uint8_t active) {
  rhodium_event::graph().reset(active != 0);
}
extern "C" void rhodium_event_node(std::uint32_t site, std::uint64_t sequence,
                                    std::uint64_t cycle, std::uint32_t width) {
  rhodium_event::graph().record_node({site, sequence}, cycle, width);
}
extern "C" void rhodium_event_payload(std::uint32_t site, std::uint64_t sequence,
                                       std::uint32_t index, std::uint32_t word) {
  rhodium_event::graph().record_payload({site, sequence}, index, word);
}
extern "C" void rhodium_event_edge(std::uint32_t child, std::uint64_t child_sequence,
                                    std::uint32_t parent, std::uint64_t parent_sequence) {
  rhodium_event::graph().record_edge({parent, parent_sequence}, {child, child_sequence});
}
