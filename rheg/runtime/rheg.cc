// Collects rheg DPI callbacks and exports timed snapshots and settled-cycle batches.
#include "rheg.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <limits>

namespace rheg {
void validate_capture_schema(const Manifest& manifest) {
  if (manifest.fields.empty()) return; // Legacy snapshots remain readable.
  if (manifest.fields.size() != manifest.payload_widths.size())
    throw std::runtime_error("capture schema site count mismatch");
  for (std::size_t site = 0; site < manifest.fields.size(); ++site) {
    std::uint64_t remaining = manifest.payload_widths[site];
    std::set<std::string> names;
    for (const auto& field : manifest.fields[site]) {
      if (field.name.empty() || !names.insert(field.name).second)
        throw std::runtime_error("duplicate or empty capture field name");
      if (field.name == "cycle" || field.name == "sequence")
        throw std::runtime_error("reserved capture field name: " + field.name);
      auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
      if (!letter(field.name.front()) || !std::all_of(field.name.begin(), field.name.end(), [&](char c) {
            return letter(c) || (c >= '0' && c <= '9');
          })) throw std::runtime_error("capture field name must be an ASCII identifier");
      if (!field.width || field.width > remaining || field.offset != remaining - field.width)
        throw std::runtime_error("invalid capture field layout");
      if (field.encoding != "hex" && field.encoding != "unsigned" &&
          field.encoding != "signed" && field.encoding != "bool" && field.encoding != "riscv")
        throw std::runtime_error("unsupported capture encoding");
      if (field.encoding == "riscv") {
        if ((field.width != 16 && field.width != 32) ||
            (field.isa.compare(0, 5, "rv32i") && field.isa.compare(0, 5, "rv64i")) ||
            field.isa.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_") != std::string::npos)
          throw std::runtime_error("invalid RISC-V capture width or ISA");
        const auto& fields = manifest.fields[site];
        const auto pc = std::find_if(fields.begin(), fields.end(), [&](const Field& f) { return f.name == field.pc; });
        if (pc == fields.end() || (pc->encoding != "hex" && pc->encoding != "unsigned") ||
            pc->width != (field.isa.compare(0, 4, "rv32") == 0 ? 32U : 64U))
          throw std::runtime_error("RISC-V capture PC must reference an XLEN-width hex/unsigned field");
      } else if (!field.isa.empty() || !field.pc.empty()) {
        throw std::runtime_error("ISA and PC options require RISC-V capture format");
      }
      if (field.encoding == "bool" && field.width != 1)
        throw std::runtime_error("boolean capture must be one bit");
      remaining -= field.width;
    }
    if (remaining) throw std::runtime_error("incomplete capture field layout");
  }
}
FieldValue capture_field(const Node& node, const Field& field) {
  if (!node.present || !field.width || std::uint64_t(field.offset) + field.width > node.width)
    throw std::runtime_error("capture field outside present node");
  FieldValue result{field.width, field.encoding, {}};
  for (std::uint64_t bit = 0; bit < field.width; bit += 32) {
    const auto start = field.offset + bit;
    const unsigned shift = start % 32;
    auto word = std::uint64_t(node.words.at(start / 32)) >> shift;
    const auto count = std::min<std::uint64_t>(32, field.width - bit);
    if (shift && count > 32 - shift)
      word |= std::uint64_t(node.words.at(start / 32 + 1)) << (32 - shift);
    if (count < 32) word &= (std::uint64_t(1) << count) - 1;
    result.words.push_back(static_cast<std::uint32_t>(word));
  }
  return result;
}
std::string FieldValue::hex() const {
  std::string result = "0x";
  for (std::uint64_t digit = (std::uint64_t(width) + 3) / 4; digit-- > 0;)
    result += "0123456789abcdef"[(words.at(digit / 8) >> ((digit % 8) * 4)) & 15];
  return result;
}
std::uint64_t FieldValue::unsigned_value() const {
  if (width > 64) throw std::runtime_error("capture exceeds 64 bits");
  return words.at(0) | (width > 32 ? std::uint64_t(words.at(1)) << 32 : 0);
}
std::int64_t FieldValue::signed_value() const {
  auto value = unsigned_value();
  const bool negative = (value >> (width - 1)) & 1;
  if (!negative) return static_cast<std::int64_t>(value);
  if (width < 64) value |= UINT64_MAX << width;
  return -1 - static_cast<std::int64_t>(~value);
}
std::string FieldValue::decimal() const {
  const bool negative = encoding == "signed" && ((words.at((width - 1) / 32) >> ((width - 1) % 32)) & 1);
  // Double-and-add decimal digits keeps arbitrary-width captures lossless.
  std::string digits = "0";
  for (std::uint64_t bit = width; bit-- > 0;) {
    unsigned carry = ((words.at(bit / 32) >> (bit % 32)) & 1) ^ unsigned(negative);
    for (auto& digit : digits) {
      const auto value = unsigned(digit - '0') * 2 + carry;
      digit = char('0' + value % 10); carry = value / 10;
    }
    if (carry) digits += char('0' + carry);
  }
  if (negative) {
    unsigned carry = 1;
    for (auto& digit : digits) {
      const auto value = unsigned(digit - '0') + carry;
      digit = char('0' + value % 10); carry = value / 10;
    }
    if (carry) digits += '1';
    digits += '-';
  }
  return std::string(digits.rbegin(), digits.rend());
}
FieldValue Graph::field(Ref ref, const std::string& name) const {
  if (!manifest_ || ref.site >= manifest_->fields.size())
    throw std::runtime_error("node has no capture schema");
  for (const auto& field : manifest_->fields[ref.site])
    if (field.name == name) return capture_field(nodes.at(ref), field);
  throw std::runtime_error("unknown capture field: " + name);
}
static void validate_entries(const std::map<Ref, Node>& nodes,
                             const std::set<std::pair<Ref, Ref>>& edges,
                             const std::map<Ref, Node>& all_nodes,
                             const Manifest* manifest_);
Graph& graph() { static Graph value; return value; }
void Graph::clear() {
  if (streaming_) throw std::runtime_error("end event stream before clearing graph");
  nodes.clear(); edges.clear();
}
Snapshot Graph::begin_stream() {
  if (streaming_ || !nodes.empty() || !edges.empty())
    throw std::runtime_error("event stream requires an empty graph and no active stream");
  if (!timing_) throw std::runtime_error("event stream requires bound timing");
  auto header = snapshot();
  streaming_ = true;
  started_ = true;
  finished_cycle_.reset();
  return header;
}
void Graph::end_stream() {
  if (!streaming_) throw std::runtime_error("no active event stream");
  if (!pending_nodes_.empty() || !pending_edges_.empty())
    throw std::runtime_error("finish event cycle before ending stream");
  streaming_ = false;
  finished_cycle_.reset();
}
CycleBatch Graph::finish_cycle(std::uint64_t cycle) {
  if (!streaming_) throw std::runtime_error("no active event stream");
  if (finished_cycle_ && cycle <= *finished_cycle_)
    throw std::runtime_error("event stream cycle must increase");
  CycleBatch batch{cycle, {}, pending_edges_};
  for (const auto& ref : pending_nodes_) {
    const auto& node = nodes.at(ref);
    if (!node.present) throw std::runtime_error("incomplete event node or payload");
    if (node.cycle > cycle || (finished_cycle_ && node.cycle <= *finished_cycle_))
      throw std::runtime_error("event node outside unfinished cycle interval");
    batch.nodes.emplace(ref, node);
  }
  for (const auto& edge : pending_edges_)
    if (!pending_nodes_.count(edge.second))
      throw std::runtime_error("event edge child already streamed");
  // Validate only new entries, resolving older parents against retained nodes.
  validate_entries(batch.nodes, batch.edges, nodes, manifest_.get());
  pending_nodes_.clear();
  pending_edges_.clear();
  finished_cycle_ = cycle;
  return batch;
}
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
  validate_capture_schema(manifest);
  manifest_ = std::make_shared<const Manifest>(manifest);
}
static void validate_entries(const std::map<Ref, Node>& nodes,
                             const std::set<std::pair<Ref, Ref>>& edges,
                             const std::map<Ref, Node>& all_nodes,
                             const Manifest* manifest_) {
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
    if (!all_nodes.count(edge.first) || !all_nodes.count(edge.second))
      throw std::runtime_error("event edge has no corresponding node");
    if (manifest_ && !manifest_->dependencies.count({edge.first.site, edge.second.site}))
      throw std::runtime_error("event edge is not a permitted manifest dependency: " +
                               std::to_string(edge.first.site) + " -> " + std::to_string(edge.second.site));
    if (all_nodes.at(edge.first).cycle > all_nodes.at(edge.second).cycle)
      throw std::runtime_error("event parent occurs after child");
  }
}
void Graph::validate() const {
  validate_entries(nodes, edges, nodes, manifest_.get());
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
static std::string occurrences_json(const std::map<Ref, Node>& nodes,
                                    const std::set<std::pair<Ref, Ref>>& edges) {
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
std::string Graph::json() const {
  validate();
  return occurrences_json(nodes, edges);
}
std::string CycleBatch::json() const {
  auto occurrences = occurrences_json(nodes, edges);
  occurrences.pop_back(); // One complete JSON object per line for pipe consumers.
  return "{\"format\":\"rhodium-event-cycle\",\"version\":1,\"cycle\":\"" +
         std::to_string(cycle) + "\",\"occurrences\":" + occurrences + "}\n";
}
void Graph::record_node(Ref ref, std::uint64_t cycle, std::uint32_t width) {
  if (streaming_ && finished_cycle_ && cycle <= *finished_cycle_)
    throw std::runtime_error("event node cycle already streamed");
  started_ = true;
  epoch_active_ = true;
  auto& node = nodes[ref];
  if (node.present) throw std::runtime_error("duplicate event identity");
  node.present = true;
  node.cycle = cycle;
  node.width = width;
  if (streaming_) pending_nodes_.insert(ref);
}
void Graph::record_payload(Ref ref, std::uint32_t index, std::uint32_t word) {
  if (streaming_ && nodes.count(ref) && nodes.at(ref).present && !pending_nodes_.count(ref))
    throw std::runtime_error("event payload node already streamed");
  started_ = true;
  epoch_active_ = true;
  auto& words = nodes[ref].words;
  if (!words.emplace(index, word).second) throw std::runtime_error("duplicate event payload word");
  if (streaming_) pending_nodes_.insert(ref);
}
void Graph::record_edge(Ref parent, Ref child) {
  if (streaming_ && nodes.count(child) && nodes.at(child).present && !pending_nodes_.count(child))
    throw std::runtime_error("event edge child already streamed");
  started_ = true;
  epoch_active_ = true;
  edges.insert({parent, child});
  if (streaming_) pending_edges_.insert({parent, child});
}
void Graph::reset(bool active) {
  if (active && streaming_ && (epoch_active_ || finished_cycle_ || !pending_nodes_.empty() || !pending_edges_.empty()))
    throw std::runtime_error("end event stream before reset");
  started_ = true;
  if (active) {
    if (epoch_active_ && timing_) {
      if (timing_->epoch_id == std::numeric_limits<std::uint64_t>::max())
        throw std::runtime_error("event epoch identity exhausted");
      ++timing_->epoch_id;
    }
    nodes.clear(); edges.clear();
    epoch_active_ = false;
  } else {
    epoch_active_ = true;
  }
}
}

extern "C" void rheg_reset(std::uint8_t active) {
  rheg::graph().reset(active != 0);
}
extern "C" void rheg_node(std::uint32_t site, std::uint64_t sequence,
                                    std::uint64_t cycle, std::uint32_t width) {
  rheg::graph().record_node({site, sequence}, cycle, width);
}
extern "C" void rheg_payload(std::uint32_t site, std::uint64_t sequence,
                                       std::uint32_t index, std::uint32_t word) {
  rheg::graph().record_payload({site, sequence}, index, word);
}
extern "C" void rheg_edge(std::uint32_t child, std::uint64_t child_sequence,
                                    std::uint32_t parent, std::uint64_t parent_sequence) {
  rheg::graph().record_edge({parent, parent_sequence}, {child, child_sequence});
}
