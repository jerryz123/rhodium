// Tests the shared C++ encoder, strict snapshot parser, and failed-output contract.
// SPDX-License-Identifier: Apache-2.0
#include "rheg_perfetto.h"
#include <zlib.h>
#include <array>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>

using namespace rheg;
namespace {
void check(bool value, const char* message = "Perfetto test expectation failed") {
  if (!value) throw std::runtime_error(message);
}
// Inspect wire costs independently of native import's semantic graph assertions.
struct WireField { unsigned tag; std::uint64_t value; std::string_view bytes; };
std::vector<WireField> wire_fields(std::string_view data) {
  std::size_t cursor = 0;
  auto varint = [&] {
    std::uint64_t n = 0;
    for (unsigned shift = 0; shift < 64; shift += 7) {
      check(cursor < data.size());
      const auto byte = static_cast<unsigned char>(data[cursor++]);
      n |= std::uint64_t(byte & 127) << shift;
      if (!(byte & 128)) return n;
    }
    throw std::runtime_error("invalid wire varint");
  };
  std::vector<WireField> fields;
  while (cursor < data.size()) {
    const auto key = varint(); const auto value = varint();
    WireField field{static_cast<unsigned>(key >> 3), value, {}};
    check((key & 7) == 0 || (key & 7) == 2);
    if ((key & 7) == 2) {
      check(value <= data.size() - cursor);
      field.bytes = data.substr(cursor, value); cursor += value;
    }
    fields.push_back(field);
  }
  return fields;
}
std::pair<unsigned, unsigned> flow_counts(std::string_view trace) {
  std::pair<unsigned, unsigned> result{};
  for (auto packet : wire_fields(trace)) if (packet.tag == 1)
    for (auto field : wire_fields(packet.bytes)) if (field.tag == 11)
      for (auto event : wire_fields(field.bytes)) if (event.tag == 6)
        for (auto legacy : wire_fields(event.bytes)) if (legacy.tag == 2) {
          if (legacy.value == 's') ++result.first;
          if (legacy.value == 'f') ++result.second;
        }
  return result;
}
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
std::string inflate_trace(const std::string& encoded) {
  z_stream state{};
  check(inflateInit2(&state, 15 + 16) == Z_OK);
  state.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(encoded.data()));
  state.avail_in = encoded.size();
  std::string decoded;
  int status;
  do {
    std::array<char, 4096> buffer;
    state.next_out = reinterpret_cast<Bytef*>(buffer.data()); state.avail_out = buffer.size();
    status = inflate(&state, Z_NO_FLUSH);
    decoded.append(buffer.data(), buffer.size() - state.avail_out);
  } while (status == Z_OK && (state.avail_in || state.avail_out == 0));
  const auto remaining = state.avail_in;
  inflateEnd(&state);
  check(remaining == 0 && status == Z_STREAM_END);
  return decoded;
}
void qualified_labels(const std::string& path) {
  Manifest descriptor{R"({"format":"rhodium-event-graph","version":1,"top":"Labels","sites":[{"id":"front","label":"frontend.s0.request","payload_width":0,"fields":[]},{"id":"back","label":"backend.s0.request","payload_width":0,"fields":[]},{"id":"plain","label":"plain","payload_width":0,"fields":[]},{"id":"trailing","label":"trailing.","payload_width":0,"fields":[]}],"dependencies":[]})",
      {0,0,0,0}, {}, {{},{},{},{}}};
  Graph graph; graph.bind_manifest(descriptor); graph.bind_timing({100000000});
  std::ostringstream live;
  PerfettoWriter writer(live, descriptor, {100000000});
  for (unsigned site = 0; site < 4; ++site) {
    graph.record_node({site,0},site,0);
    writer.write(batch(site,{site,0}));
  }
  writer.finish();
  std::istringstream input(graph.snapshot().json());
  std::ostringstream replay;
  write_perfetto(replay, read_event_trace(input));
  check(live.str() == replay.str(), "qualified label live/replay bytes differ");
  std::ofstream file(path,std::ios::binary); file << live.str(); file.close(); check(bool(file));
}
void hierarchical_labels(const std::string& path) {
  Manifest descriptor{R"({"format":"rhodium-event-graph","version":1,"top":"Hierarchy","sites":[
    {"id":"req","label":"dcache/chi.txreq","payload_width":0,"fields":[]},
    {"id":"dat","label":"dcache/chi.rxdat","payload_width":0,"fields":[]},
    {"id":"blocked","label":"ignored/group/stall","kind":"stall","observation_of":"dat","payload_width":0,"fields":[]},
    {"id":"duplicate","label":"dcache/chi.rxdat","payload_width":0,"fields":[]},
    {"id":"nested","label":"x/y.b/c","payload_width":0,"fields":[]},
    {"id":"collision","label":"dcache","payload_width":0,"fields":[]},
    {"id":"other","label":"other/y.b/c","payload_width":0,"fields":[]},
    {"id":"physical/instance/event:7","payload_width":0,"fields":[]},
    {"id":"trailing","label":"x/trailing.","payload_width":0,"fields":[]}
  ],"dependencies":[{"parent":"req","child":"dat"},{"parent":"req","child":"blocked"}]})",
    std::vector<std::uint32_t>(9,0), {{0,1},{0,2}}, std::vector<std::vector<Field>>(9)};
  Graph graph; graph.bind_manifest(descriptor); graph.bind_timing({100000000}); graph.begin_stream();
  std::ostringstream live, zipped;
  PerfettoWriter writer(live,descriptor,{100000000});
  PerfettoWriter compressed(zipped,descriptor,{100000000},PerfettoCompression::Gzip);
  for (unsigned cycle = 0; cycle < 5; ++cycle) {
    if (cycle == 0) graph.record_node({0,0},cycle,0);
    else if (cycle < 4) {
      Ref ref = cycle < 3 ? Ref{2,cycle-1} : Ref{1,0};
      graph.record_node(ref,cycle,0); graph.record_edge({0,0},ref);
    } else for (unsigned site = 3; site < 9; ++site) graph.record_node({site,0},cycle,0);
    auto settled = graph.finish_cycle(cycle); writer.write(settled); compressed.write(settled);
    if (cycle == 2) {
      std::ofstream prefix(path+".prefix",std::ios::binary); prefix << live.str(); prefix.close(); check(bool(prefix));
    }
  }
  writer.finish(); compressed.finish();
  std::istringstream input(graph.snapshot().json());
  std::ostringstream replay; write_perfetto(replay,read_event_trace(input));
  check(live.str() == replay.str(), "hierarchy live/replay differs");
  check(live.str() == inflate_trace(zipped.str()), "hierarchy gzip differs");
  // Parent descriptors must precede children, with disjoint UUIDs and no merging.
  std::set<std::uint64_t> descriptors;
  for (const auto& packet : wire_fields(live.str())) if (packet.tag == 1)
    for (const auto& field : wire_fields(packet.bytes)) if (field.tag == 60) {
      std::uint64_t uuid = 0, parent = 0, merging = 0;
      for (const auto& item : wire_fields(field.bytes)) {
        if (item.tag == 1) uuid = item.value;
        if (item.tag == 5) parent = item.value;
        if (item.tag == 15) merging = item.value;
      }
      check(uuid && (!parent || descriptors.count(parent)) && descriptors.insert(uuid).second && merging == 2);
    }
  check(descriptors.size() == 14, "unexpected hierarchy descriptor count");
  std::ofstream file(path,std::ios::binary); file << live.str(); file.close(); check(bool(file));
  std::ofstream gzip(path+".gz",std::ios::binary); gzip << zipped.str(); gzip.close(); check(bool(gzip));
  for (const auto& label : {"/x", "x/", "x//y", ""}) {
    auto invalid = manifest();
    const auto at = invalid.json.find("\"label\":\"accepted\"");
    invalid.json.replace(at,18,std::string("\"label\":\"")+label+"\"");
    for (auto mode : {PerfettoCompression::None, PerfettoCompression::Gzip}) {
      std::ostringstream output;
      rejects([&] { PerfettoWriter bad(output,invalid,{100000000},mode); }, "empty hierarchy segment");
      check(output.str().empty(), "invalid hierarchy wrote output");
    }
    Graph saved; saved.bind_manifest(invalid); saved.bind_timing({100000000});
    rejects([&] { std::istringstream source(saved.snapshot().json()); read_event_trace(source); }, "empty hierarchy segment");
  }
}
void compression_contract(const std::string& path) {
  std::ostringstream raw, compressed;
  PerfettoWriter plain(raw, manifest(), {100000000});
  PerfettoWriter zipped(compressed, manifest(), {100000000}, PerfettoCompression::Gzip);
  for (std::uint64_t cycle = 0; cycle < 3; ++cycle) {
    CycleBatch empty{cycle, {}, {}};
    const auto prefix = compressed.str();
    plain.write(empty); zipped.write(empty);
    check(compressed.str() == prefix, "empty batch added gzip framing");
  }
  CycleBatch large{10002, {}, {}};
  std::uint32_t random = 1;
  for (std::uint64_t i = 0; i < 10000; ++i) {
    random ^= random << 13; random ^= random >> 17; random ^= random << 5;
    large.nodes.emplace(Ref{0, i}, Node{true, i + 3, 8, {{0, random & 255}}});
  }
  auto invalid = large; invalid.nodes.begin()->second.width = 9;
  const auto before = compressed.str();
  rejects([&] { zipped.write(invalid); }, "width mismatch");
  check(compressed.str() == before);
  plain.write(large); zipped.write(large);
  check(compressed.str().size() > before.size(), "gzip did not emit incrementally");
  plain.finish(); zipped.finish();
  check(inflate_trace(compressed.str()) == raw.str(), "gzip round trip differs");
  check(compressed.str().size() < raw.str().size() / 2);
  const auto finalized = compressed.str();
  zipped.finish(); check(compressed.str() == finalized);
  rejects([&] { zipped.write({10003, {}, {}}); }, "already finished");
  rejects([&] { plain.write({10003, {}, {}}); }, "already finished");
  std::ofstream file(path, std::ios::binary); file << compressed.str(); file.close(); check(bool(file));
  Graph empty; empty.bind_manifest(manifest()); empty.bind_timing({1});
  std::ostringstream empty_raw, empty_gzip;
  write_perfetto(empty_raw, empty.snapshot());
  write_perfetto(empty_gzip, empty.snapshot(), PerfettoCompression::Gzip);
  check(inflate_trace(empty_gzip.str()) == empty_raw.str());
  for (bool trailer : {false, true}) {
    std::ostringstream broken;
    PerfettoWriter failing(broken, manifest(), {1}, PerfettoCompression::Gzip);
    broken.setstate(std::ios::badbit);
    rejects([&] { if (trailer) failing.finish(); else failing.write(batch(0, {0, 0}, 8)); }, "write failed");
    broken.clear();
    rejects([&] { failing.finish(); }, "previously failed");
    rejects([&] { failing.write(batch(1, {0, 1}, 8)); }, "previously failed");
  }
}
Manifest instruction_manifest(const std::string& isa, unsigned xlen, unsigned width = 32, bool multiple = false) {
  return {"{\"format\":\"rhodium-event-graph\",\"version\":1,\"top\":\"Instructions\",\"sites\":[{\"id\":\"cpu\",\"label\":\"decode\",\"payload_width\":" + std::to_string(xlen+width+32) +
    ",\"fields\":[{\"name\":\"address\",\"width\":" + std::to_string(xlen) + ",\"offset\":" + std::to_string(width+32) + ",\"encoding\":\"hex\"},"
    "{\"name\":\"opcode\",\"width\":" + std::to_string(width) + ",\"offset\":32,\"encoding\":\"riscv\",\"isa\":\"" + isa + "\",\"pc\":\"address\"},"
    "{\"name\":\"instruction\",\"width\":32,\"offset\":0,\"encoding\":\"" + (multiple ? "riscv\",\"isa\":\"" + isa + "\",\"pc\":\"address" : "hex") + "\"}]}],\"dependencies\":[]}",
    {xlen+width+32}, {}, {{{"address",xlen,width+32,"hex"}, {"opcode",width,32,"riscv",isa,"address"}, {"instruction",32,0,multiple ? "riscv" : "hex",multiple ? isa : "",multiple ? "address" : ""}}}};
}
void instruction_trace(const std::string& path, const std::string& isa, unsigned xlen, unsigned width = 32, bool multiple = false) {
  Graph graph; graph.bind_manifest(instruction_manifest(isa, xlen, width, multiple)); graph.bind_timing({100000000});
  graph.begin_stream();
  std::ostringstream live; PerfettoWriter writer(live, graph.snapshot().manifest(), {100000000});
  const std::vector<std::uint32_t> opcodes = width == 16 ? std::vector<std::uint32_t>{1,0} :
    std::vector<std::uint32_t>{0x00500513,0xf1402573,0x0080006f,0x0080006f,0x00053503,0x00b50553,0x0001,0xffffffff,0xffdff06f,0x00000517};
  for (std::size_t i = 0; i < opcodes.size(); ++i) {
    const auto pc = i == 3 ? 0x2000 : i == 8 ? 0 : 0x1000;
    const __uint128_t capture = (__uint128_t(pc) << (width+32)) | (std::uint64_t(opcodes[i]) << 32) | opcodes[i];
    const auto total = xlen+width+32;
    graph.record_node({0,i}, i, total);
    for (unsigned word = 0; word < (total+31)/32; ++word)
      graph.record_payload({0,i}, word, static_cast<std::uint32_t>(capture >> (32*word)));
    check(graph.field({0,i}, "opcode").unsigned_value() == opcodes[i]);
    writer.write(graph.finish_cycle(i));
  }
  graph.end_stream();
  std::istringstream saved(graph.snapshot().json());
  const auto snapshot = read_event_trace(saved);
  std::ostringstream replay; write_perfetto(replay, snapshot); check(live.str() == replay.str());
  check(flow_counts(live.str()) == std::make_pair(0U, 0U)); // This site cannot parent any event.
  std::ofstream file(path, std::ios::binary); file << live.str(); file.close(); check(bool(file));
}
void enum_trace(const std::string& path) {
  Manifest manifest{R"({"format":"rhodium-event-graph","version":1,"top":"Enums","sites":[
    {"id":"req","label":"cache.txreq","payload_width":7,"fields":[{"name":"opcode","width":7,"offset":0,"encoding":"enum","symbols":[{"value":"1","name":"ReadShared"},{"value":"2","name":"ReadClean"}],"label":true}]},
    {"id":"rsp","label":"cache.txrsp","payload_width":7,"fields":[{"name":"operation","width":7,"offset":0,"encoding":"enum","symbols":[{"value":"1","name":"SnpResp"},{"value":"2","name":"CompAck"}],"label":true}]},
    {"id":"plain","label":"unselected","payload_width":7,"fields":[{"name":"opcode","width":7,"offset":0,"encoding":"enum","symbols":[{"value":"1","name":"NotALabel"}]}]},
    {"id":"wide","label":"wide","payload_width":64,"fields":[{"name":"opcode","width":64,"offset":0,"encoding":"enum","symbols":[{"value":"18446744073709551615","name":"All"}],"label":true}]}
    ],"dependencies":[]})", {7,7,7,64}, {}, {
      {{"opcode",7,0,"enum","","",{{1,"ReadShared"},{2,"ReadClean"}},true}},
      {{"operation",7,0,"enum","","",{{1,"SnpResp"},{2,"CompAck"}},true}},
      {{"opcode",7,0,"enum","","",{{1,"NotALabel"}}}},
      {{"opcode",64,0,"enum","","",{{UINT64_MAX,"All"}},true}}
    }};
  Graph graph; graph.bind_manifest(manifest); graph.bind_timing({100000000}); graph.begin_stream();
  std::ostringstream live; PerfettoWriter writer(live, manifest, {100000000});
  for (std::uint64_t cycle = 0; cycle < 3; ++cycle) {
    const unsigned value = cycle == 2 ? 127 : cycle + 1;
    for (unsigned site = 0; site < 4; ++site) {
      graph.record_node({site,cycle}, cycle, site == 3 ? 64 : 7);
      graph.record_payload({site,cycle}, 0, site == 3 ? UINT32_MAX : value);
      if (site == 3) graph.record_payload({site,cycle}, 1, UINT32_MAX);
    }
    writer.write(graph.finish_cycle(cycle));
  }
  graph.end_stream();
  std::istringstream saved(graph.snapshot().json());
  std::ostringstream replay; write_perfetto(replay, read_event_trace(saved));
  check(live.str() == replay.str(), "enum live/replay bytes differ");
  std::ofstream file(path, std::ios::binary); file << live.str(); file.close(); check(bool(file));
  auto mismatch = manifest; mismatch.fields[0][0].symbols[0].second = "Different";
  rejects([&] { PerfettoWriter invalid(replay, mismatch, {100000000}); }, "descriptor differs");
  mismatch = manifest; mismatch.fields[0][0].label = false;
  rejects([&] { PerfettoWriter invalid(replay, mismatch, {100000000}); }, "descriptor differs");
  for (const auto& substitution : std::vector<std::pair<std::string,std::string>>{
      {"\"symbols\":[", "\"symbols\":false,\"ignored\":["},
      {"\"value\":\"1\"", "\"value\":true"},
      {"\"label\":true", "\"label\":\"yes\""}}) {
    auto invalid = manifest;
    invalid.json.replace(invalid.json.find(substitution.first), substitution.first.size(), substitution.second);
    try { PerfettoWriter rejected(replay, invalid, {100000000}); }
    catch (const std::exception&) { continue; }
    check(false, "invalid enum JSON was accepted");
  }
  // A selected enum, regardless of its field name, wins over an instruction mnemonic.
  auto override_manifest = instruction_manifest("rv64i", 64);
  auto& chosen = override_manifest.fields[0][2];
  chosen.encoding = "enum"; chosen.symbols = {{0x00500513,"Explicit"}}; chosen.label = true;
  const std::string old_encoding = "\"encoding\":\"hex\"";
  override_manifest.json.replace(override_manifest.json.rfind(old_encoding), old_encoding.size(),
      "\"encoding\":\"enum\",\"symbols\":[{\"value\":\"5244179\",\"name\":\"Explicit\"}],\"label\":true");
  Graph override_graph; override_graph.bind_manifest(override_manifest); override_graph.bind_timing({100000000});
  override_graph.record_node({0,0}, 0, 128);
  override_graph.record_payload({0,0}, 0, 0x00500513);
  override_graph.record_payload({0,0}, 1, 0x00500513);
  override_graph.record_payload({0,0}, 2, 0x1000);
  override_graph.record_payload({0,0}, 3, 0);
  std::ofstream explicit_file(path + ".override", std::ios::binary);
  write_perfetto(explicit_file, override_graph.snapshot()); explicit_file.close(); check(bool(explicit_file));
}
void interning_trace(const std::string& path) {
  Graph graph; graph.bind_manifest(instruction_manifest("rv64i", 64)); graph.bind_timing({100000000});
  graph.begin_stream();
  std::ostringstream live; PerfettoWriter writer(live, graph.snapshot().manifest(), {100000000});
  // More unique PCs than dictionary capacity; then reuse an early and a late PC.
  // Assembly remains the same throughout, including after inline fallback begins.
  for (std::uint64_t i = 0; i < 5002; ++i) {
    const auto pc = i == 5000 ? 0x1000 : i == 5001 ? 0x1000 + 4 * 4999 : 0x1000 + 4 * i;
    graph.record_node({0,i}, i, 128);
    graph.record_payload({0,i}, 0, 0x00500513);
    graph.record_payload({0,i}, 1, 0x00500513);
    graph.record_payload({0,i}, 2, pc);
    graph.record_payload({0,i}, 3, 0);
    writer.write(graph.finish_cycle(i));
  }
  graph.end_stream();
  std::ostringstream replay; write_perfetto(replay, graph.snapshot());
  check(live.str() == replay.str(), "interning live/replay bytes differ");
  const auto encoded = live.str();
  const auto assembly = encoded.find("li a0, 5"); check(assembly != std::string::npos);
  check(encoded.find("li a0, 5", assembly + 1) == std::string::npos, "assembly was not interned");
  unsigned values = 0;
  for (auto packet : wire_fields(encoded))
    for (auto field : wire_fields(packet.bytes)) if (field.tag == 12)
      for (auto entry : wire_fields(field.bytes)) if (entry.tag == 29) ++values;
  check(values == 4096, "interned value dictionary did not respect its capacity");
  check(flow_counts(encoded) == std::make_pair(0U, 0U));
  std::ofstream file(path, std::ios::binary); file << encoded; file.close(); check(bool(file));
}
void stall_trace(const std::string& path) {
  Manifest descriptor{R"({"format":"rhodium-event-graph","version":1,"top":"Stalls","sites":[{"id":"accepted","label":"accepted","payload_width":0},{"id":"issued","label":"issued","kind":"transfer","payload_width":0},{"id":"issued/stall","label":"issued.stall","kind":"stall","observation_of":"issued","payload_width":0}],"dependencies":[{"parent":"accepted","child":"issued"},{"parent":"accepted","child":"issued/stall"}]})",
                      {0, 0, 0}, {{0, 1}, {0, 2}}};
  Graph graph; graph.bind_manifest(descriptor); graph.bind_timing({100000000}); graph.begin_stream();
  std::ostringstream live; PerfettoWriter writer(live, descriptor, {100000000});
  auto conflict = batch(0, {1, 0});
  conflict.nodes.emplace(Ref{2, 0}, Node{true, 0, 0, {}});
  const auto prefix = live.str();
  rejects([&] { writer.write(conflict); }, "multiple occurrences on a shared track");
  check(live.str() == prefix, "rejected shared-track collision wrote output");
  graph.record_node({0, 0}, 0, 0); writer.write(graph.finish_cycle(0));
  for (std::uint64_t cycle = 1; cycle <= 2; ++cycle) {
    graph.record_node({2, cycle - 1}, cycle, 0);
    graph.record_edge({0, 0}, {2, cycle - 1});
    writer.write(graph.finish_cycle(cycle));
  }
  graph.record_node({1, 0}, 3, 0); graph.record_edge({0, 0}, {1, 0}); writer.write(graph.finish_cycle(3));
  // A withdrawn/new offer need not have any accepted ancestor.
  graph.record_node({2, 2}, 4, 0); writer.write(graph.finish_cycle(4));
  writer.finish(); graph.end_stream();
  std::istringstream saved(graph.snapshot().json());
  std::ostringstream replay; write_perfetto(replay, read_event_trace(saved));
  check(live.str() == replay.str(), "stall live/replay bytes differ");
  std::ostringstream compressed;
  write_perfetto(compressed, graph.snapshot(), PerfettoCompression::Gzip);
  check(inflate_trace(compressed.str()) == live.str(), "stall gzip/replay bytes differ");
  check(flow_counts(live.str()) == std::make_pair(1U, 2U), "stall runs must share parent arrows without becoming sources");
  std::ofstream file(path, std::ios::binary); file << live.str(); file.close(); check(bool(file));
  auto invalid = [&](const std::string& from, const std::string& to, const std::string& diagnostic) {
    auto bad = descriptor;
    auto pos = bad.json.find(from); check(pos != std::string::npos);
    bad.json.replace(pos, from.size(), to);
    std::ostringstream output;
    rejects([&] { PerfettoWriter rejected(output, bad, {100000000}); }, diagnostic);
    check(output.str().empty());
  };
  invalid("\"kind\":\"stall\"", "\"kind\":\"unknown\"", "unsupported event kind");
  invalid("\"observation_of\":\"issued\"", "\"observation_of\":\"\"", "stall requires observation_of");
  invalid("\"observation_of\":\"issued\"", "\"observation_of\":\"missing\"", "stall must observe a transfer");
  invalid("\"observation_of\":\"issued\"", "\"observation_of\":\"issued/stall\"", "stall must observe a transfer");
  invalid("\"kind\":\"stall\"", "\"kind\":\"transfer\"", "transfer must not observe");
  invalid("\"parent\":\"accepted\"", "\"parent\":\"issued/stall\"", "stall cannot supply downstream lineage");
}
void named_stall_trace(const std::string& path) {
  // The observer precedes its transfer, has a different capture schema, and is
  // the only active site. Neither disassembly nor an enum may rename its slice.
  Manifest descriptor{R"({"format":"rhodium-event-graph","version":1,"top":"NamedStalls","sites":[
    {"id":"cpu/stall","label":"decode.stall","kind":"stall","observation_of":"cpu","payload_width":99,"fields":[
      {"name":"pc","width":64,"offset":35,"encoding":"hex"},
      {"name":"instruction","width":32,"offset":3,"encoding":"riscv","isa":"rv64i","pc":"pc"},
      {"name":"reason","width":1,"offset":2,"encoding":"enum","symbols":[{"value":"1","name":"Blocked"}],"label":true},
      {"name":"rheg","width":1,"offset":1,"encoding":"bool"},
      {"name":"rheg_site","width":1,"offset":0,"encoding":"bool"}]},
    {"id":"cpu","label":"decode","payload_width":0,"fields":[]}],"dependencies":[]})",
    {99,0}, {}, {{{"pc",64,35,"hex"},{"instruction",32,3,"riscv","rv64i","pc"},
                 {"reason",1,2,"enum","","",{{1,"Blocked"}},true},
                 {"rheg",1,1,"bool"},{"rheg_site",1,0,"bool"}}, {}}};
  Graph graph; graph.bind_manifest(descriptor); graph.bind_timing({100000000});
  graph.record_node({0,0}, 0, 99);
  const __uint128_t payload = (__uint128_t(0x1000) << 35) | (std::uint64_t(0x00500513) << 3) | 7;
  for (unsigned i = 0; i < 4; ++i) graph.record_payload({0,0}, i, static_cast<std::uint32_t>(payload >> (i*32)));
  std::ofstream file(path, std::ios::binary); write_perfetto(file, graph.snapshot()); file.close(); check(bool(file));
}
void stall_runs(const std::string& path) {
  Manifest descriptor{R"({"format":"rhodium-event-graph","version":1,"top":"Runs","sites":[
    {"id":"source","payload_width":8},{"id":"issue","payload_width":8},
    {"id":"blocked","kind":"stall","observation_of":"issue","payload_width":8},
    {"id":"other","payload_width":8},
    {"id":"other-blocked","kind":"stall","observation_of":"other","payload_width":8}],
    "dependencies":[{"parent":"source","child":"issue"},{"parent":"source","child":"blocked"},{"parent":"other","child":"blocked"}]})",
    {8,8,8,8,8}, {{0,1},{0,2},{3,2}}};
  std::vector<CycleBatch> cycles;
  for (unsigned c = 0; c < 13; ++c) cycles.push_back({c, {}, {}});
  auto put = [&](unsigned cycle, Ref ref, unsigned value, std::initializer_list<Ref> parents = {}) {
    cycles[cycle].nodes.emplace(ref, Node{true, cycle, 8, {{0,value}}});
    for (auto parent : parents) cycles[cycle].edges.insert({parent,ref});
  };
  put(0,{0,0},42); put(0,{3,0},42);
  put(1,{2,0},42,{{0,0}}); put(2,{2,1},42,{{0,0}});
  for (unsigned c = 1; c <= 3; ++c) put(c,{4,c-1},42);
  put(3,{2,2},43,{{0,0}}); // Captures changed, even though the parent did not.
  put(4,{0,1},42); put(4,{2,3},43,{{0,1}}); // New parent with identical payload.
  put(5,{2,4},43,{{0,1},{3,0}}); put(6,{2,5},43,{{3,0},{0,1}}); // Parent set, not order.
  put(8,{2,6},43); // A gap and withdrawn ancestry.
  put(9,{1,0},43,{{0,1}}); // Transfer closes the observation.
  put(10,{2,7},43); put(11,{2,9},43); put(12,{2,10},43); // Sequence gap splits the run.
  Graph graph; graph.bind_manifest(descriptor); graph.bind_timing({100000000}); graph.begin_stream();
  std::ostringstream live, zipped;
  PerfettoWriter writer(live, descriptor, {100000000});
  PerfettoWriter gzip(zipped, descriptor, {100000000}, PerfettoCompression::Gzip);
  for (const auto& cycle : cycles) {
    if (cycle.cycle == 2) {
      const auto before = live.str();
      auto bad = cycle; bad.nodes.begin()->second.width = 7;
      rejects([&] { writer.write(bad); }, "width mismatch");
      check(live.str() == before, "invalid batch mutated an open stall run");
    }
    for (const auto& [ref,node] : cycle.nodes) {
      graph.record_node(ref,node.cycle,node.width);
      for (const auto& [index,word] : node.words) graph.record_payload(ref,index,word);
    }
    for (const auto& [parent,child] : cycle.edges) graph.record_edge(parent,child);
    auto settled = graph.finish_cycle(cycle.cycle);
    writer.write(settled); gzip.write(settled);
    if (cycle.cycle == 2 || cycle.cycle == 7) {
      std::ofstream prefix(path + ".prefix" + std::to_string(cycle.cycle), std::ios::binary);
      prefix << live.str(); prefix.close(); check(bool(prefix));
    }
  }
  writer.finish(); gzip.finish(); graph.end_stream();
  const auto snapshot = graph.snapshot();
  check(snapshot.nodes().size() == 17 && snapshot.edges().size() == 9, "coalescing changed the graph");
  std::istringstream saved(snapshot.json()); std::ostringstream replay;
  write_perfetto(replay,read_event_trace(saved));
  check(replay.str() == live.str() && inflate_trace(zipped.str()) == live.str(), "coalesced live/replay/gzip differ");
  for (unsigned size : {2,5,13}) {
    std::ostringstream output; PerfettoWriter grouped(output,descriptor,{100000000});
    CycleBatch pending{};
    for (const auto& c : cycles) {
      pending.cycle = c.cycle; pending.nodes.insert(c.nodes.begin(),c.nodes.end()); pending.edges.insert(c.edges.begin(),c.edges.end());
      if ((c.cycle+1)%size == 0 || c.cycle == 12) { grouped.write(pending); pending = {}; }
    }
    grouped.finish(); check(output.str() == live.str(), "coalescing depends on batch boundaries");
  }
  check(flow_counts(live.str()) == std::make_pair(3U,6U));
  std::ofstream file(path,std::ios::binary); file << live.str(); file.close(); check(bool(file));
  for (auto mode : {PerfettoCompression::None,PerfettoCompression::Gzip}) {
    std::ostringstream output; PerfettoWriter broken(output,descriptor,{100000000},mode);
    broken.write(batch(0,{2,0},8)); output.setstate(std::ios::badbit);
    rejects([&] { broken.finish(); }, "write failed"); output.clear();
    rejects([&] { broken.finish(); }, "previously failed");
  }
  std::ostringstream maximum; PerfettoWriter terminal(maximum,descriptor,{UINT64_MAX});
  terminal.write(batch(UINT64_MAX-1,{2,UINT64_MAX-1},8));
  terminal.write(batch(UINT64_MAX,{2,UINT64_MAX},8)); terminal.finish();
  std::ofstream last(path+".maximum",std::ios::binary); last << maximum.str(); last.close(); check(bool(last));
}
}
int main(int argc, char** argv) {
  check(argc == 2);
  {
    auto descriptor = manifest();
    const auto ending = descriptor.json.rfind('}');
    descriptor.json.insert(ending, ",\"gaps\":[{\"site\":\"root/issued\",\"boundary\":\"opaque.output\",\"reason\":\"opaque-boundary\"}]");
    Graph partial; partial.bind_manifest(descriptor); partial.bind_timing({100000000}); partial.begin_stream();
    std::ostringstream live; PerfettoWriter writer(live, descriptor, {100000000});
    partial.record_node({0,0},0,8); partial.record_payload({0,0},0,42); writer.write(partial.finish_cycle(0));
    partial.record_unknown({1,0}); partial.record_edge({0,0},{1,0}); partial.record_node({1,0},1,0);
    writer.write(partial.finish_cycle(1)); writer.finish();
    std::istringstream input(partial.snapshot().json());
    auto replayed = read_event_trace(input);
    check(replayed.nodes().at({1,0}).ancestry_unknown);
    std::ostringstream replay; write_perfetto(replay,replayed);
    check(live.str()==replay.str(), "partial live/replay differs");
    std::ofstream file(std::string(argv[1])+"/partial.pftrace",std::ios::binary); file << live.str(); file.close(); check(bool(file));
    auto malformed = partial.snapshot().json();
    malformed.replace(malformed.find("\"ancestry_unknown\":true"), 23, "\"ancestry_unknown\":1");
    rejects([&] { std::istringstream bad(malformed); read_event_trace(bad); }, "ancestry_unknown must be boolean");
  }
  qualified_labels(std::string(argv[1]) + "/qualified-labels.pftrace");
  hierarchical_labels(std::string(argv[1]) + "/hierarchy.pftrace");
  {
    Manifest descriptor{R"({"format":"rhodium-event-graph","version":1,"top":"PartialStalls","sites":[{"id":"transfer","label":"issue","payload_width":false},{"id":"stall","label":"issue.stall","payload_width":false,"kind":"stall","observation_of":"transfer"}],"dependencies":[]})", {0,0}, {}};
    Graph graph; graph.bind_manifest(descriptor); graph.bind_timing({100000000}); graph.begin_stream();
    std::ostringstream live; PerfettoWriter writer(live,descriptor,{100000000});
    for (unsigned cycle=0; cycle<3; ++cycle) {
      graph.record_node({1,cycle},cycle,0);
      if (cycle) graph.record_unknown({1,cycle});
      writer.write(graph.finish_cycle(cycle));
    }
    writer.finish();
    std::istringstream input(graph.snapshot().json()); std::ostringstream replay;
    write_perfetto(replay,read_event_trace(input));
    check(live.str()==replay.str(),"unknown stall boundary differs in replay");
    std::ofstream file(std::string(argv[1])+"/partial-stalls.pftrace",std::ios::binary); file << live.str(); file.close(); check(bool(file));
  }
  stall_trace(std::string(argv[1]) + "/stalls.pftrace");
  named_stall_trace(std::string(argv[1]) + "/named-stalls.pftrace");
  stall_runs(std::string(argv[1]) + "/stall-runs.pftrace");
  compression_contract(std::string(argv[1]) + "/compressed.pftrace.gz");
  enum_trace(std::string(argv[1]) + "/enums.pftrace");
  interning_trace(std::string(argv[1]) + "/interning.pftrace");
  instruction_trace(std::string(argv[1]) + "/riscv64.pftrace", "rv64imafdc_zicsr", 64);
  instruction_trace(std::string(argv[1]) + "/riscv64-properties.pftrace", "rv64imafdcb_za64rs_zba_zbb_zbs_zcmop_zic64b_zicbop_zicboz_zawrs_zihintpause_zihintntl_zicntr_zicond_zicsr_zifencei_zihpm_zimop_zkt", 64);
  instruction_trace(std::string(argv[1]) + "/riscv32.pftrace", "rv32i", 32);
  instruction_trace(std::string(argv[1]) + "/riscv16.pftrace", "rv32ic", 32, 16);
  instruction_trace(std::string(argv[1]) + "/multiple-instructions.pftrace", "rv64imafdc_zicsr", 64, 32, true);
  std::ostringstream invalid_isa_output;
  rejects([&] { PerfettoWriter w(invalid_isa_output, instruction_manifest("rv64i_znotreal", 64), {1}); }, "invalid RISC-V ISA");
  rejects([&] { PerfettoWriter w(invalid_isa_output, instruction_manifest("rv64i_zic64bogus", 64), {1}); }, "invalid RISC-V ISA");
  rejects([&] { PerfettoWriter w(invalid_isa_output, instruction_manifest("rv64i_zic64b_znotreal", 64), {1}); }, "invalid RISC-V ISA");
  check(invalid_isa_output.str().empty());
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
  check(flow_counts(output.str()) == std::make_pair(3U, 2U)); // Includes the same-site continuation.
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
  auto terminal = manifest();
  const std::string self_edge = ",{\"parent\":\"root/issued\",\"child\":\"root/issued\"}";
  const auto self_pos = terminal.json.find(self_edge); check(self_pos != std::string::npos);
  terminal.json.erase(self_pos, self_edge.size()); terminal.dependencies.erase({1,1});
  std::ostringstream terminal_output; PerfettoWriter terminal_writer(terminal_output, terminal, {300000000});
  terminal_writer.write(first);
  auto unused_parent = batch(1, {0, 1}, 8);
  terminal_writer.write(unused_parent); // Potential source still needs a start, even without a known child.
  auto delayed_child = second; delayed_child.cycle = 2; delayed_child.nodes.begin()->second.cycle = 2;
  terminal_writer.write(delayed_child);
  check(flow_counts(terminal_output.str()) == std::make_pair(2U, 1U)); // Terminal issued site has no start.
  std::ofstream terminal_file(std::string(argv[1]) + "/terminal.pftrace", std::ios::binary);
  terminal_file << terminal_output.str(); terminal_file.close(); check(bool(terminal_file));
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
