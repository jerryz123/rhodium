// Tests the shared C++ encoder, strict snapshot parser, and failed-output contract.
#include "rheg_perfetto.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

using namespace rheg;
namespace {
void check(bool value) { if (!value) throw std::runtime_error("Perfetto test expectation failed"); }
template<class F> void rejects(F action, const std::string& text) {
  try { action(); } catch (const std::exception& e) {
    if (std::string(e.what()).find(text) != std::string::npos) return;
    throw;
  }
  throw std::runtime_error("expected rejection: " + text);
}
Manifest manifest() {
  return {R"({"format":"rhodium-event-graph","version":1,"top":"Test","sites":[{"id":"root/accepted","label":"accepted","source_location":"fixture.rhdl:17","payload_width":8},{"id":"root/issued","label":"issued","payload_width":0}],"dependencies":[{"parent":"root/accepted","child":"root/issued"},{"parent":"root/issued","child":"root/issued"}]})",
          {8, 0}, {{0, 1}, {1, 1}}};
}
CycleBatch batch(std::uint64_t cycle, Ref ref, unsigned width = 0) {
  return {cycle, {{ref, {true, cycle, width, width ? std::map<std::uint32_t, std::uint32_t>{{0, 42}} : std::map<std::uint32_t, std::uint32_t>{}}}}, {}};
}
}
int main(int argc, char** argv) {
  check(argc == 2);
  std::ostringstream output;
  rejects([&] { PerfettoWriter w(output, manifest(), {0}); }, "positive clock");
  check(output.str().empty());
  auto bad_manifest = manifest(); bad_manifest.payload_widths[0] = 9;
  rejects([&] { PerfettoWriter w(output, bad_manifest, {1}); }, "differs from JSON");
  PerfettoWriter writer(output, manifest(), {300000000, UINT64_MAX});
  auto first = batch(0, {0, 9007199254740993ULL}, 8);
  writer.write(first);
  auto prefix = output.str();
  rejects([&] { writer.write(first); }, "watermark");
  auto second = batch(1, {1, 0});
  second.edges.insert({{0, 9007199254740993ULL}, {1, 0}});
  auto invalid = second; invalid.nodes.begin()->second.cycle = 0;
  rejects([&] { writer.write(invalid); }, "outside unfinished");
  invalid = second; invalid.nodes.begin()->second.width = 8;
  rejects([&] { writer.write(invalid); }, "width mismatch");
  invalid = second; invalid.edges.insert({{0, 999}, {1, 0}});
  rejects([&] { writer.write(invalid); }, "missing edge");
  invalid = second; invalid.edges.insert({{1, 0}, {1, 0}});
  rejects([&] { writer.write(invalid); }, "cyclic");
  check(output.str() == prefix);
  writer.write(second);
  auto third = batch(2, {1, 1});
  third.edges.insert({{1, 0}, {1, 1}});
  writer.write(third);
  check(output.str().substr(0, prefix.size()) == prefix);
  std::ofstream precision(std::string(argv[1]) + "/precision.pftrace", std::ios::binary);
  precision << output.str(); precision.close(); check(bool(precision));

  std::ostringstream broken;
  PerfettoWriter failure(broken, manifest(), {1});
  broken.setstate(std::ios::badbit);
  rejects([&] { failure.write(first); }, "write failed");
  broken.clear();
  rejects([&] { failure.write(first); }, "previously failed");
  std::ostringstream overflow;
  PerfettoWriter huge(overflow, manifest(), {1});
  auto largest = batch(UINT64_MAX, {0, 0}, 8);
  rejects([&] { huge.write(largest); }, "timestamp overflow");
  const auto before_overflow = overflow.str();
  auto end_overflow = batch(INT64_MAX / 1000000000, {0, 0}, 8);
  rejects([&] { huge.write(end_overflow); }, "timestamp overflow");
  check(overflow.str() == before_overflow);
  std::ostringstream last_cycle;
  PerfettoWriter maximum(last_cycle, manifest(), {UINT64_MAX});
  maximum.write(largest); // N+1 is computed wide, never wrapped to cycle zero.
  std::ofstream boundary(std::string(argv[1]) + "/last-cycle.pftrace", std::ios::binary);
  boundary << last_cycle.str(); boundary.close(); check(bool(boundary));
  auto padding = first; padding.nodes.begin()->second.words[0] = 256;
  rejects([&] { huge.write(padding); }, "padding");
  huge.write(first); // Rejected end boundaries must not advance the writer.

  Graph graph;
  graph.bind_manifest(manifest()); graph.bind_timing({300000000, UINT64_MAX});
  for (const auto& b : {first, second, third}) {
    for (const auto& [ref, node] : b.nodes) {
      graph.record_node(ref, node.cycle, node.width);
      for (auto word : node.words) graph.record_payload(ref, word.first, word.second);
    }
    for (auto edge : b.edges) graph.record_edge(edge.first, edge.second);
  }
  std::istringstream source(graph.snapshot().json());
  const auto snapshot = read_event_trace(source);
  check(snapshot.timing()->epoch_id == UINT64_MAX);
  std::ostringstream replay;
  write_perfetto(replay, snapshot);
  check(replay.str() == output.str());
  auto repeated = manifest();
  auto repeated_label = repeated.json.find("\"label\":\"issued\"");
  check(repeated_label != std::string::npos);
  repeated.json.replace(repeated_label, 16, "\"label\":\"accepted\"");
  std::ofstream named(std::string(argv[1]) + "/repeated-label.pftrace", std::ios::binary);
  PerfettoWriter repeated_writer(named, repeated, {300000000});
  for (const auto& b : {first, second, third}) repeated_writer.write(b);
  named.close(); check(bool(named));
  for (const auto& text : {std::string("{\"format\":1,\"format\":2}"),
                           graph.snapshot().json() + " garbage"}) {
    std::istringstream malformed(text);
    rejects([&] { read_event_trace(malformed); }, "");
  }
  for (const auto& replacement : {std::string("-1"), std::string("1.0"), std::string("true"),
                                  std::string("\"18446744073709551616\"")}) {
    auto text = graph.snapshot().json();
    auto pos = text.find("\"300000000\""); check(pos != std::string::npos);
    text.replace(pos, 11, replacement);
    std::istringstream malformed(text);
    rejects([&] { read_event_trace(malformed); }, "");
  }
  std::istringstream deep(std::string(300, '[') + "0" + std::string(300, ']'));
  rejects([&] { read_event_trace(deep); }, "nesting limit");
  for (const auto& change : std::vector<std::pair<std::string, std::string>>{
           {"\"width\":8", "\"width\":4294967296"},
           {"\"words\":[42]", "\"words\":[]"},
           {"\"site\":0", "\"site\":7"},
           {"\"cycle-zero\"", "\"wall-clock\""}}) {
    auto text = graph.snapshot().json();
    auto pos = text.find(change.first); check(pos != std::string::npos);
    text.replace(pos, change.first.size(), change.second);
    std::istringstream malformed(text);
    rejects([&] { read_event_trace(malformed); }, "");
  }
  auto unicode = manifest();
  auto label = unicode.json.find("\"label\":\"accepted\""); check(label != std::string::npos);
  unicode.json.replace(label, 18, R"("label":"\u03bb\n")");
  std::ostringstream unicode_output;
  PerfettoWriter unicode_writer(unicode_output, unicode, {1});
  unicode_writer.write(first);
  check(unicode_output.str().find("λ\n") != std::string::npos);
  Graph empty; empty.bind_manifest(manifest()); empty.bind_timing({1});
  std::ostringstream empty_trace; write_perfetto(empty_trace, empty.snapshot());
  check(!empty_trace.str().empty());
  std::ofstream empty_file(std::string(argv[1]) + "/empty.pftrace", std::ios::binary);
  empty_file << empty_trace.str(); empty_file.close(); check(bool(empty_file));
  Manifest named_manifest{
    R"({"format":"rhodium-event-graph","version":1,"top":"Named","sites":[{"id":"capture","payload_width":8,"fields":[{"name":"value","width":8,"offset":0,"encoding":"unsigned"}]}],"dependencies":[]})",
    {8}, {}, {{{"value",8,0,"unsigned"}}}};
  Graph named_graph; named_graph.bind_manifest(named_manifest); named_graph.bind_timing({1});
  named_graph.record_node({0,0},0,8); named_graph.record_payload({0,0},0,255);
  std::istringstream named_input(named_graph.snapshot().json());
  check(read_event_trace(named_input).field({0,0},"value").unsigned_value() == 255);
  auto mismatch = named_manifest; mismatch.fields[0][0].name = "different";
  rejects([&] { PerfettoWriter w(empty_trace, mismatch, {1}); }, "differs from JSON");
  for (const auto& reserved : {"cycle", "sequence"}) {
    auto text = named_graph.snapshot().json();
    const auto pos = text.find("\"name\":\"value\""); check(pos != std::string::npos);
    text.replace(pos, 14, std::string("\"name\":\"") + reserved + "\"");
    std::istringstream invalid_capture(text);
    rejects([&] { read_event_trace(invalid_capture); },
            std::string("reserved capture field name: ") + reserved);
  }
  for (const auto& change : std::vector<std::pair<std::string,std::string>>{
         {"\"offset\":0", "\"offset\":1"}, {"\"offset\":0", "\"offset\":-1"},
         {"\"width\":8", "\"width\":0"}, {"\"width\":8", "\"width\":1.5"},
         {"\"width\":8", "\"width\":4294967296"}, {"\"name\":\"value\"", "\"name\":\"\""},
         {"\"encoding\":\"unsigned\"", "\"encoding\":\"float\""},
         {"\"encoding\":\"unsigned\"", "\"encoding\":\"bool\""}}) {
    auto text = named_graph.snapshot().json();
    const auto pos = text.find(change.first); check(pos != std::string::npos);
    text.replace(pos, change.first.size(), change.second);
    std::istringstream invalid_capture(text);
    rejects([&] { read_event_trace(invalid_capture); }, "");
  }
  std::cout << "C++ Perfetto writer and snapshot parser tests passed\n";
}
