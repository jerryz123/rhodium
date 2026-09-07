// Encodes Perfetto v58.2 packets and parses rheg trace snapshots in C++.
#include "rheg_perfetto.h"
#include <nlohmann/json.hpp>
#include <charconv>
#include <algorithm>
#include <limits>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace rheg {
namespace {
using Json = nlohmann::json;
void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
std::uint64_t number(const Json& value, std::uint64_t maximum = UINT64_MAX) {
  std::uint64_t result = 0;
  if (value.is_string()) {
    const auto& s = value.get_ref<const std::string&>();
    require(!s.empty() && s.find_first_not_of("0123456789") == std::string::npos,
            "expected unsigned decimal integer");
    auto parsed = std::from_chars(s.data(), s.data() + s.size(), result);
    require(parsed.ec == std::errc() && parsed.ptr == s.data() + s.size(), "integer overflow");
  } else {
    require(value.is_number_unsigned() || (value.is_number_integer() && value.get<std::int64_t>() >= 0),
            "expected unsigned integer, not floating-point or boolean");
    result = value.get<std::uint64_t>();
  }
  require(result <= maximum, "integer out of range");
  return result;
}
Json parse(std::istream& input) {
  std::vector<std::set<std::string>> keys;
  auto result = Json::parse(input, [&](int depth, Json::parse_event_t event, Json& value) {
    require(depth < 256, "trace JSON nesting limit exceeded");
    if (event == Json::parse_event_t::object_start) keys.emplace_back();
    if (event == Json::parse_event_t::key)
      require(keys.back().insert(value.get<std::string>()).second, "duplicate JSON key");
    if (event == Json::parse_event_t::object_end) keys.pop_back();
    return true;
  });
  require(!input.bad(), "trace input read failed");
  return result;
}
Json parse(const std::string& text) {
  std::istringstream input(text);
  return parse(input);
}
void format(const Json& value, const char* name) {
  require(value.at("format") == name && number(value.at("version")) == 1, "unsupported trace format/version");
}
struct Site { std::string id, label, source; };
struct Description {
  Manifest manifest;
  std::string top;
  std::vector<Site> sites;
};
Description describe(const Json& json) {
  format(json, "rhodium-event-graph");
  Description result;
  result.manifest.json = json.dump();
  result.top = json.at("top").get<std::string>();
  const auto& sites = json.at("sites");
  require(sites.is_array() && !sites.empty() && sites.size() < INT32_MAX, "invalid manifest site table");
  std::map<std::string, std::uint32_t> ids;
  for (const auto& site : sites) {
    const auto id = site.at("id").get<std::string>();
    require(!id.empty() && ids.emplace(id, ids.size()).second, "duplicate or empty site identity");
    result.sites.push_back({id, site.value("label", id), site.value("source_location", std::string("<unknown>"))});
    const auto& width = site.at("payload_width");
    result.manifest.payload_widths.push_back(width == Json(false) ? 0 : number(width, UINT32_MAX));
  }
  require(json.at("dependencies").is_array(), "dependencies must be an array");
  for (const auto& edge : json.at("dependencies")) {
    auto parent = ids.find(edge.at("parent").get<std::string>());
    auto child = ids.find(edge.at("child").get<std::string>());
    require(parent != ids.end() && child != ids.end(), "unknown manifest dependency site");
    result.manifest.dependencies.emplace(parent->second, child->second);
  }
  return result;
}
Ref ref(const Json& value) {
  require(value.is_array() && value.size() == 2, "invalid event reference");
  return {static_cast<std::uint32_t>(number(value[0], UINT32_MAX)), number(value[1])};
}
// Minimal protobuf wire encoder: only unsigned varints and length-delimited
// fields used by this exporter. Field numbers/types are from the pinned schema:
// https://github.com/google/perfetto/tree/v58.2/protos/perfetto/trace
// No generated code, recording service, protobuf runtime, or SDK is required.
void varint(std::string& out, std::uint64_t n) {
  while (n >= 128) { out.push_back(static_cast<char>((n & 127) | 128)); n >>= 7; }
  out.push_back(static_cast<char>(n));
}
void integer(std::string& out, unsigned field, std::uint64_t n) {
  varint(out, std::uint64_t(field) << 3); varint(out, n);
}
void bytes(std::string& out, unsigned field, const std::string& value) {
  varint(out, (std::uint64_t(field) << 3) | 2); varint(out, value.size()); out += value;
}
void packet(std::string& out, const std::string& value) { bytes(out, 1, value); }
void annotation(std::string& event, const std::string& name, const std::string& value) {
  std::string arg;
  bytes(arg, 10, name); bytes(arg, 6, value); bytes(event, 4, arg);
}
}

struct PerfettoWriter::Impl {
  std::ostream& output;
  Description description;
  TraceTiming timing;
  std::map<Ref, std::pair<std::uint64_t, std::uint64_t>> known; // flow identity, cycle
  std::optional<std::uint64_t> watermark;
  bool failed = false;

  Impl(std::ostream& out, const Manifest& manifest, TraceTiming clock)
      : output(out), description(describe(parse(manifest.json))), timing(clock) {
    require(timing.clock_frequency_hz != 0, "positive clock frequency required");
    require(description.manifest.payload_widths == manifest.payload_widths &&
            description.manifest.dependencies == manifest.dependencies, "manifest descriptor differs from JSON");
    std::string stream, descriptor, process, p;
    integer(descriptor, 1, description.sites.size() + 1);
    integer(process, 1, 1); bytes(process, 6, description.top);
    bytes(descriptor, 3, process); bytes(p, 60, descriptor); packet(stream, p);
    for (std::size_t i = 0; i < description.sites.size(); ++i) {
      std::string track, pkt;
      integer(track, 1, i + 1); integer(track, 5, description.sites.size() + 1);
      bytes(track, 2, description.sites[i].label); integer(track, 15, 2);
      bytes(track, 14, description.sites[i].id);
      bytes(pkt, 60, track); packet(stream, pkt);
    }
    flush(stream);
  }
  void flush(const std::string& data) {
    require(!failed, "Perfetto output previously failed");
    require(data.size() <= static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()), "Perfetto batch too large");
    try {
      output.write(data.data(), static_cast<std::streamsize>(data.size()));
      output.flush();
      require(bool(output), "Perfetto output write failed");
    } catch (...) { failed = true; throw; }
  }
  std::uint64_t timestamp(__uint128_t cycle) const {
    const auto ns = cycle * 1000000000 / timing.clock_frequency_hz;
    require(ns <= INT64_MAX, "Perfetto timestamp overflow");
    return static_cast<std::uint64_t>(ns);
  }
  void event(std::string& stream, Ref ref, __uint128_t cycle, std::string fields) const {
    integer(fields, 11, std::uint64_t(ref.site) + 1);
    std::string pkt;
    integer(pkt, 8, timestamp(cycle)); integer(pkt, 58, 6); // Synthetic BOOTTIME ns.
    integer(pkt, 10, 1); bytes(pkt, 11, fields); packet(stream, pkt);
  }
  void flow(std::string& stream, Ref ref, std::uint64_t cycle, char phase, std::uint64_t id) const {
    std::string fields, legacy;
    bytes(fields, 23, "dependency"); bytes(fields, 22, "rhodium.flow");
    integer(legacy, 2, phase); integer(legacy, 6, id); integer(legacy, 12, 1);
    bytes(fields, 6, legacy); event(stream, ref, cycle, fields);
  }
  void write(const CycleBatch& batch) {
    require(!failed, "Perfetto output previously failed");
    require(!watermark || batch.cycle > *watermark, "cycle watermark must increase");
    std::map<Ref, std::set<Ref>> parents, children;
    std::map<Ref, std::size_t> indegree;
    for (const auto& [ref, node] : batch.nodes) {
      require(!known.count(ref) && ref.site < description.sites.size(), "duplicate or unknown event node");
      require(node.cycle <= batch.cycle && (!watermark || node.cycle > *watermark), "node outside unfinished cycle interval");
      require(node.present && node.width == description.manifest.payload_widths[ref.site], "incomplete node or payload width mismatch");
      require(node.words.size() == (std::uint64_t(node.width) + 31) / 32, "incomplete payload");
      for (std::size_t i = 0; i < node.words.size(); ++i) require(node.words.count(i), "missing payload word");
      require(!(node.width % 32) || !(node.words.rbegin()->second >> (node.width % 32)), "nonzero payload padding");
      indegree[ref] = 0;
      timestamp(node.cycle);
      timestamp(static_cast<__uint128_t>(node.cycle) + 1);
    }
    for (const auto& [parent, child] : batch.edges) {
      require(batch.nodes.count(child) && (batch.nodes.count(parent) || known.count(parent)), "missing edge endpoint or child already streamed");
      require(description.manifest.dependencies.count({parent.site, child.site}), "edge not permitted by manifest");
      const auto cycle = batch.nodes.count(parent) ? batch.nodes.at(parent).cycle : known.at(parent).second;
      require(cycle <= batch.nodes.at(child).cycle, "parent occurs after child");
      parents[child].insert(parent);
      if (batch.nodes.count(parent)) { children[parent].insert(child); ++indegree[child]; }
    }
    std::set<std::pair<std::uint64_t, Ref>> ready;
    for (const auto& [ref, degree] : indegree) if (!degree) ready.emplace(batch.nodes.at(ref).cycle, ref);
    std::vector<Ref> order;
    while (!ready.empty()) {
      const auto ref = ready.begin()->second; ready.erase(ready.begin()); order.push_back(ref);
      for (auto child : children[ref]) if (!--indegree[child]) ready.emplace(batch.nodes.at(child).cycle, child);
    }
    require(order.size() == batch.nodes.size(), "cyclic same-cycle dependencies");
    std::map<Ref, std::pair<std::uint64_t, std::uint64_t>> additions;
    std::string stream;
    for (auto ref : order) {
      const auto& node = batch.nodes.at(ref);
      require(known.size() < UINT64_MAX - additions.size(), "flow identity exhaustion");
      const auto id = known.size() + additions.size() + 1;
      additions.emplace(ref, std::make_pair(id, node.cycle));
      std::string fields;
      integer(fields, 9, 1); bytes(fields, 23, description.sites[ref.site].label);
      bytes(fields, 22, "rhodium.event");
      annotation(fields, "site", std::to_string(ref.site));
      annotation(fields, "sequence", std::to_string(ref.sequence));
      annotation(fields, "cycle", std::to_string(node.cycle));
      annotation(fields, "epoch_id", std::to_string(timing.epoch_id));
      annotation(fields, "clock_frequency_hz", std::to_string(timing.clock_frequency_hz));
      annotation(fields, "payload_width", std::to_string(node.width));
      std::vector<std::uint32_t> words;
      for (auto word : node.words) words.push_back(word.second);
      annotation(fields, "payload_words_lsw_first", Json(words).dump());
      annotation(fields, "site_id", description.sites[ref.site].id);
      annotation(fields, "source_location", description.sites[ref.site].source);
      event(stream, ref, node.cycle, fields);
      for (auto parent : parents[ref]) {
        const auto identity = additions.count(parent) ? additions.at(parent).first : known.at(parent).first;
        flow(stream, ref, node.cycle, 'f', identity);
      }
      flow(stream, ref, node.cycle, 's', id);
      fields.clear(); integer(fields, 9, 2);
      event(stream, ref, static_cast<__uint128_t>(node.cycle) + 1, fields);
    }
    flush(stream);
    known.merge(additions); // Transfer already allocated nodes after successful I/O.
    watermark = batch.cycle;
  }
};
PerfettoWriter::PerfettoWriter(std::ostream& output, const Manifest& manifest, TraceTiming timing)
    : impl_(std::make_unique<Impl>(output, manifest, timing)) {}
PerfettoWriter::~PerfettoWriter() = default;
void PerfettoWriter::write(const CycleBatch& batch) { impl_->write(batch); }

Snapshot read_event_trace(std::istream& input) {
  const auto json = parse(input);
  format(json, "rhodium-event-trace");
  const auto description = describe(json.at("manifest"));
  const auto& timing = json.at("timing");
  require(timing.at("origin") == "cycle-zero", "unsupported timing origin");
  Graph graph;
  graph.bind_manifest(description.manifest);
  graph.bind_timing({number(timing.at("clock_frequency_hz")), number(timing.at("epoch_id"))});
  const auto& occurrences = json.at("occurrences");
  format(occurrences, "rhodium-event-occurrences");
  require(occurrences.at("nodes").is_array() && occurrences.at("edges").is_array(), "nodes/edges must be arrays");
  for (const auto& node : occurrences.at("nodes")) {
    Ref ref{static_cast<std::uint32_t>(number(node.at("site"), UINT32_MAX)), number(node.at("sequence"))};
    graph.record_node(ref, number(node.at("cycle")), number(node.at("width"), UINT32_MAX));
    require(node.at("words").is_array() && node.at("words").size() <= UINT32_MAX, "invalid payload words");
    std::uint32_t index = 0;
    for (const auto& word : node.at("words")) graph.record_payload(ref, index++, number(word, UINT32_MAX));
  }
  for (const auto& edge : occurrences.at("edges")) graph.record_edge(ref(edge.at("parent")), ref(edge.at("child")));
  return graph.snapshot();
}
void write_perfetto(std::ostream& output, const Snapshot& snapshot) {
  require(snapshot.timing().has_value(), "Perfetto export requires timing");
  CycleBatch batch{0, snapshot.nodes(), snapshot.edges()};
  for (const auto& entry : batch.nodes) batch.cycle = std::max(batch.cycle, entry.second.cycle);
  PerfettoWriter writer(output, snapshot.manifest(), *snapshot.timing());
  writer.write(batch);
}
}
