// Encodes Perfetto v58.2 packets and parses rheg trace snapshots in C++.
#include "rheg_perfetto.h"
#include <nlohmann/json.hpp>
#include <disasm.h>
#include <zlib.h>
#include <charconv>
#include <algorithm>
#include <array>
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
struct Site { std::string id, label, source, kind, observation_of; };
struct Description {
  Manifest manifest;
  std::string top;
  std::vector<Site> sites;
  std::vector<std::uint32_t> track_sites;
  std::vector<bool> shared_tracks;
};
Description describe(const Json& json) {
  format(json, "rhodium-event-graph");
  Description result;
  result.manifest.json = json.dump();
  result.top = json.at("top").get<std::string>();
  const auto& sites = json.at("sites");
  require(sites.is_array() && !sites.empty() && sites.size() < INT32_MAX, "invalid manifest site table");
  std::map<std::string, std::uint32_t> ids;
  const bool named = sites.front().contains("fields");
  for (const auto& site : sites) {
    const auto id = site.at("id").get<std::string>();
    require(!id.empty() && ids.emplace(id, ids.size()).second, "duplicate or empty site identity");
    result.sites.push_back({id, site.value("label", id), site.value("source_location", std::string("<unknown>")),
                           site.value("kind", std::string("transfer")), site.value("observation_of", std::string())});
    const auto& display = result.sites.back();
    require(display.kind == "transfer" || display.kind == "stall", "unsupported event kind");
    require((display.kind == "stall") == !display.observation_of.empty(), "stall requires observation_of; transfer must not observe another site");
    const auto& width = site.at("payload_width");
    result.manifest.payload_widths.push_back(width == Json(false) ? 0 : number(width, UINT32_MAX));
    require(site.contains("fields") == named, "inconsistent capture schema presence");
    if (named) {
      const auto& fields = site.at("fields");
      require(fields.is_array(), "capture fields must be an array");
      std::vector<Field> captures;
      for (const auto& field : fields) {
        captures.push_back({field.at("name").get<std::string>(),
                           static_cast<std::uint32_t>(number(field.at("width"), UINT32_MAX)),
                           static_cast<std::uint32_t>(number(field.at("offset"), UINT32_MAX)),
                           field.at("encoding").get<std::string>(),
                           field.value("isa", std::string()), field.value("pc", std::string())});
        auto& capture = captures.back();
        capture.label = field.value("label", false);
        if (field.contains("symbols")) {
          require(field.at("symbols").is_array(), "enum symbols must be an array");
          require(capture.encoding == "enum", "symbols require enum capture format");
          for (const auto& symbol : field.at("symbols"))
            capture.symbols.emplace_back(number(symbol.at("value")), symbol.at("name").get<std::string>());
        }
      }
      result.manifest.fields.push_back(std::move(captures));
    }
  }
  validate_capture_schema(result.manifest);
  result.shared_tracks.resize(result.sites.size(), false);
  for (std::size_t i = 0; i < result.sites.size(); ++i) {
    const auto& site = result.sites[i];
    auto track = static_cast<std::uint32_t>(i);
    if (site.kind == "stall") {
      auto observed = ids.find(site.observation_of);
      require(observed != ids.end() && result.sites[observed->second].kind == "transfer",
              "stall must observe a transfer site");
      track = observed->second;
      result.shared_tracks[track] = true;
    }
    result.track_sites.push_back(track);
  }
  require(json.at("dependencies").is_array(), "dependencies must be an array");
  for (const auto& edge : json.at("dependencies")) {
    auto parent = ids.find(edge.at("parent").get<std::string>());
    auto child = ids.find(edge.at("child").get<std::string>());
    require(parent != ids.end() && child != ids.end(), "unknown manifest dependency site");
    require(result.sites[parent->second].kind != "stall", "stall cannot supply downstream lineage");
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

// One sequence owns four independent IID spaces. Admission is bounded; existing
// entries remain valid for the epoch and excess/oversized strings stay inline.
struct InternedStrings {
  enum Kind { Category, Name, AnnotationName, Value };
  struct Dictionary {
    std::map<std::string, std::uint64_t> ids;
    std::size_t bytes = 0;
  };
  std::array<Dictionary, 4> dictionaries;
};
struct PendingInterns {
  InternedStrings& known;
  InternedStrings added;
  std::string pending;
  explicit PendingInterns(InternedStrings& committed) : known(committed) {}
  void reference(std::string& message, unsigned iid_field, unsigned inline_field,
                 InternedStrings::Kind kind, const std::string& value) {
    const auto& prior = known.dictionaries[kind];
    auto& fresh = added.dictionaries[kind];
    auto found = prior.ids.find(value);
    if (found != prior.ids.end()) { integer(message, iid_field, found->second); return; }
    found = fresh.ids.find(value);
    if (found != fresh.ids.end()) { integer(message, iid_field, found->second); return; }
    if (prior.ids.size() + fresh.ids.size() >= 4096 || value.size() > 1024 ||
        prior.bytes + fresh.bytes + value.size() > 1024 * 1024) {
      bytes(message, inline_field, value);
      return;
    }
    const auto iid = prior.ids.size() + fresh.ids.size() + 1;
    fresh.ids.emplace(value, iid); fresh.bytes += value.size();
    std::string entry; integer(entry, 1, iid); bytes(entry, 2, value);
    constexpr unsigned tables[] = {1, 2, 3, 29}; // InternedData fields.
    bytes(pending, tables[kind], entry);
    integer(message, iid_field, iid);
  }
  void commit() {
    for (std::size_t i = 0; i < added.dictionaries.size(); ++i) {
      known.dictionaries[i].ids.merge(added.dictionaries[i].ids);
      known.dictionaries[i].bytes += added.dictionaries[i].bytes;
    }
  }
};
void annotation(std::string& event, PendingInterns& interns, const std::string& name,
                const std::string& value, bool intern_value = true) {
  std::string arg;
  interns.reference(arg, 1, 10, InternedStrings::AnnotationName, name);
  if (intern_value) interns.reference(arg, 17, 6, InternedStrings::Value, value);
  else bytes(arg, 6, value);
  bytes(event, 4, arg);
}
void capture_annotation(std::string& event, PendingInterns& interns, const Field& field, const Node& node) {
  const auto value = capture_field(node, field);
  const auto& name = field.name;
  if (field.encoding == "hex" || field.width > 64 ||
      ((field.encoding == "unsigned" || field.encoding == "enum") && value.unsigned_value() > INT64_MAX)) {
    annotation(event, interns, name, field.encoding == "hex" ? value.hex() : value.decimal());
  } else {
    std::string arg;
    interns.reference(arg, 1, 10, InternedStrings::AnnotationName, name);
    if (field.encoding == "bool") integer(arg, 2, value.unsigned_value());
    else if (field.encoding == "signed") integer(arg, 4, static_cast<std::uint64_t>(value.signed_value()));
    else integer(arg, 3, value.unsigned_value());
    bytes(event, 4, arg);
  }
}

// Decoder state and its bounded cache are private to one export, never the graph.
struct RiscvDecoder {
  isa_parser_t isa;
  disassembler_t decoder;
  explicit RiscvDecoder(const std::string& name) : isa(name.c_str(), "MSU"), decoder(&isa, true) {}
};
struct RiscvFormatting {
  std::map<std::string, std::unique_ptr<RiscvDecoder>> decoders;
  std::map<std::tuple<std::string, std::uint64_t, std::uint32_t, std::uint32_t>, std::string> cache;
  void prepare(const std::string& isa) {
    if (!decoders.count(isa)) decoders.emplace(isa, std::make_unique<RiscvDecoder>(isa));
  }
  std::string render(const Field& field, const Node& node, const std::vector<Field>& fields) {
    const auto value = capture_field(node, field);
    const auto bits = static_cast<std::uint32_t>(value.unsigned_value());
    const auto pc_field = std::find_if(fields.begin(), fields.end(), [&](const Field& f) { return f.name == field.pc; });
    const auto pc = capture_field(node, *pc_field).unsigned_value();
    const auto key = std::make_tuple(field.isa, pc, bits, field.width);
    if (const auto found = cache.find(key); found != cache.end()) return found->second;
    insn_t instruction(bits);
    std::string result = "unknown";
    const auto length = instruction.length();
    if ((length == 2 && (bits >> 16) == 0) || (length == 4 && field.width == 32))
      result = decoders.at(field.isa)->decoder.disassemble(instruction);
    if (result == "unknown") result = value.hex();
    else {
      // Spike expresses branch/jump targets as a final 'pc +/- offset' operand.
      // Resolve it using the explicitly associated capture, with XLEN wrapping.
      const auto plus_pos = result.find("pc + ");
      const auto minus_pos = result.find("pc - ");
      const auto target_pos = plus_pos != std::string::npos ? plus_pos : minus_pos;
      if (target_pos != std::string::npos) {
        const bool subtract = result.at(target_pos + 3) == '-';
        const auto delta = std::stoull(result.substr(target_pos + 5), nullptr, 0);
        auto target = subtract ? pc - delta : pc + delta;
        if (field.isa.compare(0, 4, "rv32") == 0) target &= UINT32_MAX;
        std::ostringstream address; address << "0x" << std::hex << target;
        result.replace(target_pos, std::string::npos, address.str());
      }
      // Normalize alignment padding into a compact single-line argument.
      std::istringstream words(result); std::string word; result.clear();
      while (words >> word) { if (!result.empty()) result += ' '; result += word; }
    }
    if (cache.size() >= 4096) cache.clear();
    cache.emplace(key, result);
    return result;
  }
};

// Persistent gzip dictionary, with bounded scratch space independent of trace size.
struct GzipEncoder {
  z_stream state{};
  GzipEncoder() {
    require(deflateInit2(&state, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8,
                         Z_DEFAULT_STRATEGY) == Z_OK, "gzip initialization failed");
  }
  ~GzipEncoder() { deflateEnd(&state); }
  int pump(std::ostream& output, int mode) {
    std::array<unsigned char, 32768> buffer;
    state.next_out = buffer.data(); state.avail_out = buffer.size();
    const int result = deflate(&state, mode);
    require(result == Z_OK || result == Z_STREAM_END, "gzip compression failed");
    output.write(reinterpret_cast<const char*>(buffer.data()), buffer.size() - state.avail_out);
    require(bool(output), "Perfetto output write failed");
    return result;
  }
  void write(std::ostream& output, const std::string& data) {
    if (data.empty()) return;
    for (std::size_t offset = 0; offset < data.size();) {
      const auto count = std::min<std::size_t>(data.size() - offset, 65536);
      state.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data() + offset));
      state.avail_in = static_cast<uInt>(count);
      while (state.avail_in) pump(output, Z_NO_FLUSH);
      offset += count;
    }
    // Keep deflate blocks open across batches. Perfetto requires the footer at
    // finish() before importing gzip, so per-cycle sync flushes buy no live view.
  }
  void finish(std::ostream& output) {
    state.next_in = nullptr; state.avail_in = 0;
    while (pump(output, Z_FINISH) != Z_STREAM_END) {}
  }
};
}

struct PerfettoWriter::Impl {
  struct StallRun {
    Ref last;
    Node node;
    std::set<Ref> parents;
  };
  std::map<std::uint32_t, StallRun> stalls; // Track site -> open visual interval.
  std::ostream& output;
  Description description;
  TraceTiming timing;
  RiscvFormatting instructions;
  InternedStrings strings;
  std::vector<bool> can_parent;
  std::map<Ref, std::pair<std::uint64_t, std::uint64_t>> known; // flow identity, cycle
  std::optional<std::uint64_t> watermark;
  bool failed = false;
  bool finished = false;
  std::unique_ptr<GzipEncoder> gzip;

  Impl(std::ostream& out, const Manifest& manifest, TraceTiming clock, PerfettoCompression compression)
      : output(out), description(describe(parse(manifest.json))), timing(clock) {
    require(timing.clock_frequency_hz != 0, "positive clock frequency required");
    require(description.manifest.payload_widths == manifest.payload_widths &&
            description.manifest.dependencies == manifest.dependencies &&
            description.manifest.fields == manifest.fields, "manifest descriptor differs from JSON");
    for (const auto& fields : manifest.fields)
      for (const auto& field : fields)
        if (field.encoding == "riscv") instructions.prepare(field.isa);
    require(compression == PerfettoCompression::None || compression == PerfettoCompression::Gzip,
            "unsupported Perfetto compression");
    if (compression == PerfettoCompression::Gzip) gzip = std::make_unique<GzipEncoder>();
    can_parent.resize(description.sites.size(), false);
    for (const auto& edge : description.manifest.dependencies) can_parent[edge.first] = true;
    std::string stream, descriptor, p;
    // Run-wide metadata precedes every occurrence, including in empty/prefix traces.
    std::string metadata, metadata_packet;
    for (const auto& entry : std::vector<std::pair<std::string,std::uint64_t>>{
           {"rheg.clock_frequency_hz", timing.clock_frequency_hz}, {"rheg.epoch_id", timing.epoch_id}}) {
      std::string item;
      bytes(item, 1, entry.first); bytes(item, 2, std::to_string(entry.second));
      bytes(metadata, 2, item); // ChromeEventBundle.metadata / ChromeMetadata.string_value.
    }
    integer(metadata_packet, 10, 1); integer(metadata_packet, 13, 1); // Clear sequence state once.
    bytes(metadata_packet, 5, metadata); packet(stream, metadata_packet);
    integer(descriptor, 1, description.sites.size() + 1);
    bytes(descriptor, 2, description.top);
    // A custom group honors child_ordering; process/thread tracks ignore it.
    integer(descriptor, 11, 1); // TrackDescriptor.child_ordering = LEXICOGRAPHIC.
    integer(descriptor, 15, 2); // SIBLING_MERGE_BEHAVIOR_NONE.
    bytes(p, 60, descriptor); packet(stream, p);
    const auto site_description = [&](std::size_t i) {
      Json site = {{"site", i}, {"site_id", description.sites[i].id},
                   {"source_location", description.sites[i].source},
                   {"payload_width", description.manifest.payload_widths[i]}};
      site["kind"] = description.sites[i].kind;
      if (description.sites[i].kind == "stall") site["observation_of"] = description.sites[i].observation_of;
      if (!description.manifest.fields.empty()) {
        site["fields"] = Json::array();
        for (const auto& field : description.manifest.fields[i]) {
          Json capture = {{"name",field.name}, {"width",field.width},
                          {"offset",field.offset}, {"encoding",field.encoding}};
          if (field.encoding == "riscv") { capture["isa"] = field.isa; capture["pc"] = field.pc; }
          if (field.encoding == "enum") {
            capture["symbols"] = Json::array();
            for (const auto& symbol : field.symbols)
              capture["symbols"].push_back({{"value",std::to_string(symbol.first)}, {"name",symbol.second}});
          }
          if (field.label) capture["label"] = true;
          site["fields"].push_back(std::move(capture));
        }
      }
      return site;
    };
    for (std::size_t i = 0; i < description.sites.size(); ++i) {
      if (description.track_sites[i] != i) continue;
      std::string track, pkt;
      integer(track, 1, i + 1); integer(track, 5, description.sites.size() + 1);
      bytes(track, 2, description.sites[i].label); integer(track, 15, 2);
      auto site = site_description(i);
      if (description.shared_tracks[i]) {
        site["observations"] = Json::array();
        for (std::size_t j = 0; j < description.sites.size(); ++j)
          if (j != i && description.track_sites[j] == i)
            site["observations"].push_back(site_description(j));
      }
      bytes(track, 14, site.dump(2)); // TrackDescriptor has description, not arbitrary annotations.
      bytes(pkt, 60, track); packet(stream, pkt);
    }
    flush(stream);
  }
  void flush(const std::string& data) {
    require(!failed, "Perfetto output previously failed");
    require(data.size() <= static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()), "Perfetto batch too large");
    try {
      if (gzip) gzip->write(output, data);
      else output.write(data.data(), static_cast<std::streamsize>(data.size()));
      output.flush();
      require(bool(output), "Perfetto output write failed");
    } catch (...) { failed = true; throw; }
  }
  void finish() {
    require(!failed, "Perfetto output previously failed");
    if (finished) return;
    try {
      PendingInterns interns(strings);
      std::string stream;
      close_stalls(stream, interns, stalls, [](const StallRun&) { return true; });
      flush(stream);
      interns.commit();
      if (gzip) gzip->finish(output);
      output.flush();
      require(bool(output), "Perfetto output write failed");
      finished = true;
    } catch (...) { failed = true; throw; }
  }
  std::uint64_t timestamp(__uint128_t cycle) const {
    const auto ns = cycle * 1000000000 / timing.clock_frequency_hz;
    require(ns <= INT64_MAX, "Perfetto timestamp overflow");
    return static_cast<std::uint64_t>(ns);
  }
  void event(std::string& stream, PendingInterns& interns, Ref ref, __uint128_t cycle, std::string fields) const {
    integer(fields, 11, std::uint64_t(description.track_sites[ref.site]) + 1);
    std::string pkt;
    integer(pkt, 8, timestamp(cycle)); integer(pkt, 58, 6); // Synthetic BOOTTIME ns.
    integer(pkt, 10, 1); integer(pkt, 13, 2); // Needs incremental state on sequence 1.
    if (!interns.pending.empty()) { bytes(pkt, 12, interns.pending); interns.pending.clear(); }
    bytes(pkt, 11, fields); packet(stream, pkt);
  }
  void flow(std::string& stream, PendingInterns& interns, Ref ref, std::uint64_t cycle, char phase, std::uint64_t id) const {
    std::string fields, legacy;
    interns.reference(fields, 10, 23, InternedStrings::Name, "dependency");
    interns.reference(fields, 3, 22, InternedStrings::Category, "rhodium.flow");
    integer(legacy, 2, phase); integer(legacy, 6, id); integer(legacy, 12, 1);
    bytes(fields, 6, legacy); event(stream, interns, ref, cycle, fields);
  }
  template<class Predicate>
  void close_stalls(std::string& stream, PendingInterns& interns,
                    std::map<std::uint32_t, StallRun>& runs, Predicate should_close) const {
    std::set<std::pair<std::uint64_t, std::uint32_t>> endings;
    for (const auto& [track, run] : runs)
      if (should_close(run)) endings.emplace(run.node.cycle, track);
    for (const auto& [cycle, track] : endings) {
      const auto& run = runs.at(track);
      std::string fields;
      integer(fields, 9, 2);
      event(stream, interns, run.last, static_cast<__uint128_t>(cycle) + 1, fields);
      runs.erase(track);
    }
  }
  void write(const CycleBatch& batch) {
    require(!failed, "Perfetto output previously failed");
    require(!finished, "Perfetto writer already finished");
    require(!watermark || batch.cycle > *watermark, "cycle watermark must increase");
    std::map<Ref, std::set<Ref>> parents, children;
    std::map<Ref, std::size_t> indegree;
    std::set<std::pair<std::uint32_t, std::uint64_t>> occupied;
    for (const auto& [ref, node] : batch.nodes) {
      require(!known.count(ref) && ref.site < description.sites.size(), "duplicate or unknown event node");
      const auto track = description.track_sites[ref.site];
      require(!description.shared_tracks[track] || occupied.emplace(track, node.cycle).second,
              "multiple occurrences on a shared track in one cycle");
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
    PendingInterns interns(strings);
    std::string stream;
    auto next_stalls = stalls; // Commit interval state only after successful I/O.
    std::optional<std::uint64_t> group_cycle;
    for (auto ref : order) {
      const auto& node = batch.nodes.at(ref);
      if (!group_cycle || *group_cycle != node.cycle) {
        group_cycle = node.cycle;
        // Decide continuations for the whole cycle before emitting any begins.
        // This preserves packet/intern order across live and snapshot batches.
        close_stalls(stream, interns, next_stalls, [&](const StallRun& run) {
          if (run.last.sequence == UINT64_MAX) return true;
          const auto next = batch.nodes.find({run.last.site, run.last.sequence + 1});
          return next == batch.nodes.end() ||
                 next->second.cycle != node.cycle ||
                 static_cast<__uint128_t>(run.node.cycle) + 1 != node.cycle ||
                 next->second.words != run.node.words ||
                 !std::equal(parents[next->first].begin(), parents[next->first].end(),
                             run.parents.begin(), run.parents.end(), [](Ref a, Ref b) {
                               return a.site == b.site && a.sequence == b.sequence;
                             });
        });
      }
      require(known.size() < UINT64_MAX - additions.size(), "flow identity exhaustion");
      const auto id = known.size() + additions.size() + 1;
      additions.emplace(ref, std::make_pair(id, node.cycle));
      const auto track = description.track_sites[ref.site];
      const bool stall = description.sites[ref.site].kind == "stall";
      if (stall && next_stalls.count(track)) {
        auto& run = next_stalls.at(track);
        run.last = ref; run.node.cycle = node.cycle;
        continue;
      }
      std::string fields;
      integer(fields, 9, 1);
      std::string mnemonic;
      std::string selected_label;
      std::size_t instruction_count = 0;
      interns.reference(fields, 3, 22, InternedStrings::Category, "rhodium.event");
      // Unique counters remain exact decimal strings, without filling the dictionary.
      annotation(fields, interns, "sequence", std::to_string(ref.sequence), false);
      annotation(fields, interns, "cycle", std::to_string(node.cycle), false);
      if (!description.manifest.fields.empty()) {
        for (const auto& field : description.manifest.fields[ref.site]) {
          if (field.encoding == "riscv") {
            const auto assembly = instructions.render(field, node, description.manifest.fields[ref.site]);
            annotation(fields, interns, field.name, assembly);
            mnemonic = assembly.substr(0, assembly.find(' '));
            ++instruction_count;
          }
          else capture_annotation(fields, interns, field, node);
          if (field.label) {
            const auto value = capture_field(node, field);
            const auto code = value.unsigned_value();
            const auto symbol = std::find_if(field.symbols.begin(), field.symbols.end(),
                [&](const auto& entry) { return entry.first == code; });
            selected_label = symbol == field.symbols.end() ? value.hex() : symbol->second;
          }
        }
      } else {
        // Old snapshots retain their raw display when no capture schema exists.
        std::vector<std::uint32_t> words;
        for (auto word : node.words) words.push_back(word.second);
        annotation(fields, interns, "payload_words_lsw_first", Json(words).dump());
      }
      // Shorten only default slice labels; tracks and decoded opcodes retain their names.
      const auto& site_label = description.sites[ref.site].label;
      const auto separator = site_label.rfind('.');
      const auto default_name = separator != std::string::npos && separator + 1 < site_label.size()
          ? site_label.substr(separator + 1) : site_label;
      // Stalls retain captures but never use instruction/transaction slice names.
      interns.reference(fields, 10, 23, InternedStrings::Name,
                        description.sites[ref.site].kind == "stall" ? "stall" :
                        !selected_label.empty() ? selected_label :
                        instruction_count == 1 ? mnemonic : default_name);
      event(stream, interns, ref, node.cycle, fields);
      for (auto parent : parents[ref]) {
        const auto identity = additions.count(parent) ? additions.at(parent).first : known.at(parent).first;
        flow(stream, interns, ref, node.cycle, 'f', identity);
      }
      if (can_parent[ref.site]) flow(stream, interns, ref, node.cycle, 's', id);
      if (stall) next_stalls.emplace(track, StallRun{ref, node, parents[ref]});
      else {
        fields.clear(); integer(fields, 9, 2);
        event(stream, interns, ref, static_cast<__uint128_t>(node.cycle) + 1, fields);
      }
    }
    close_stalls(stream, interns, next_stalls, [&](const StallRun& run) { return run.node.cycle < batch.cycle; });
    flush(stream);
    interns.commit();
    stalls.swap(next_stalls);
    known.merge(additions); // Transfer already allocated nodes after successful I/O.
    watermark = batch.cycle;
  }
};
PerfettoWriter::PerfettoWriter(std::ostream& output, const Manifest& manifest, TraceTiming timing,
                               PerfettoCompression compression)
    : impl_(std::make_unique<Impl>(output, manifest, timing, compression)) {}
PerfettoWriter::~PerfettoWriter() = default;
void PerfettoWriter::write(const CycleBatch& batch) { impl_->write(batch); }
void PerfettoWriter::finish() { impl_->finish(); }

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
void write_perfetto(std::ostream& output, const Snapshot& snapshot, PerfettoCompression compression) {
  require(snapshot.timing().has_value(), "Perfetto export requires timing");
  CycleBatch batch{0, snapshot.nodes(), snapshot.edges()};
  for (const auto& entry : batch.nodes) batch.cycle = std::max(batch.cycle, entry.second.cycle);
  PerfettoWriter writer(output, snapshot.manifest(), *snapshot.timing(), compression);
  writer.write(batch);
  writer.finish();
}
}
