// Collects DPI event callbacks independently of simulator callback ordering.
#include "rhodium_event.h"

#include <sstream>
#include <stdexcept>

namespace rhodium_event {
Graph& graph() { static Graph value; return value; }
void Graph::clear() { nodes.clear(); edges.clear(); }
void Graph::validate() const {
  for (const auto& entry : nodes) {
    const auto& node = entry.second;
    if (!node.present || node.words.size() != (std::uint64_t(node.width) + 31) / 32)
      throw std::runtime_error("incomplete event node or payload");
    for (std::uint32_t i = 0; i < node.words.size(); ++i)
      if (!node.words.count(i)) throw std::runtime_error("missing event payload word");
    if (node.width % 32 && (node.words.rbegin()->second >> (node.width % 32)))
      throw std::runtime_error("nonzero event payload padding");
  }
  for (const auto& edge : edges)
    if (!nodes.count(edge.first) || !nodes.count(edge.second))
      throw std::runtime_error("event edge has no corresponding node");
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
}

extern "C" void rhodium_event_reset(std::uint8_t active) {
  if (active) rhodium_event::graph().clear();
}
extern "C" void rhodium_event_node(std::uint32_t site, std::uint64_t sequence,
                                    std::uint64_t cycle, std::uint32_t width) {
  auto& node = rhodium_event::graph().nodes[{site, sequence}];
  if (node.present) throw std::runtime_error("duplicate event identity");
  node.present = true;
  node.cycle = cycle;
  node.width = width;
}
extern "C" void rhodium_event_payload(std::uint32_t site, std::uint64_t sequence,
                                       std::uint32_t index, std::uint32_t word) {
  auto& words = rhodium_event::graph().nodes[{site, sequence}].words;
  if (!words.emplace(index, word).second) throw std::runtime_error("duplicate event payload word");
}
extern "C" void rhodium_event_edge(std::uint32_t child, std::uint64_t child_sequence,
                                    std::uint32_t parent, std::uint64_t parent_sequence) {
  rhodium_event::graph().edges.insert({{parent, parent_sequence}, {child, child_sequence}});
}
