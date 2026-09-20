// Reconstructs exact last-arriving-packet parents from public line-engine handshakes.
// SPDX-License-Identifier: Apache-2.0
#include "../../../../../rheg/runtime/rheg.h"
#include "rv5stage-compack_manifest.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace {
rheg::Graph expected;
std::map<unsigned, std::uint64_t> sequences;
std::array<unsigned, 2> packets{};
std::array<std::optional<rheg::Ref>, 2> owner;
std::array<std::optional<rheg::Ref>, 2> residency;
unsigned completed_refills = 0;
std::uint64_t cycle = 0;
unsigned acks = 0, roms = 0, stalls = 0, resets = 0, nonmax_final = 0;
bool rom = false;
[[noreturn]] void fail(const char* message) { std::fprintf(stderr, "%s\n", message); std::abort(); }
rheg::Ref node(unsigned site, unsigned width, unsigned value) {
  rheg::Ref ref{site, sequences[site]++};
  auto& n = expected.nodes[ref];
  n.present = true; n.cycle = cycle; n.width = width; n.words[0] = value;
  return ref;
}
rheg::Ref begin_refill(unsigned site, unsigned address) {
  const auto& manifest = rheg_generated::manifest();
  const rheg::Ref ref{site, sequences[site]++};
  auto& n = expected.nodes[ref];
  n.present = true; n.cycle = cycle; n.width = manifest.payload_widths.at(site);
  n.ancestry_unknown = true; // Scalar bench stimulus has no upstream Flow occurrence.
  for (unsigned word = 0; word < (n.width + 31) / 32; ++word) n.words[word] = 0;
  for (const auto& field : manifest.fields.at(site)) {
    const std::uint64_t value = field.name == "address" ? address : 2;
    for (unsigned bit = 0; bit < field.width; ++bit)
      n.words[(field.offset + bit) / 32] |= unsigned((value >> bit) & 1) << ((field.offset + bit) % 32);
  }
  return ref;
}
void sample_lane(unsigned lane, unsigned fire, unsigned packet, unsigned ack, unsigned dbid) {
  if (ack) {
    if (!owner[lane] || packets[lane] != 15 || (lane == 1 && rom)) fail("CompAck without coherent complete line");
    const auto child = node(lane == 0 ? test_sites::dack : test_sites::iack, 12, dbid);
    expected.edges.insert({*owner[lane], child});
    owner[lane].reset(); ++acks;
  }
  if (fire) {
    if (packet > 3 || (packets[lane] & (1u << packet))) fail("duplicate or invalid received packet");
    const auto ref = node(lane == 0 ? test_sites::ddata : test_sites::idata, 2, packet);
    packets[lane] |= 1u << packet;
    if (packets[lane] == 15) {
      nonmax_final += packet != 3;
      if (lane == 0 || !rom) owner[lane] = ref;
    }
  }
}
}
extern "C" void compack_bind() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
extern "C" void compack_sample(unsigned reset, unsigned command, unsigned instruction_rom,
    unsigned dfire, unsigned dpacket, unsigned ifire, unsigned ipacket,
    unsigned dack, unsigned ddbid, unsigned iack, unsigned idbid, unsigned stalled,
    unsigned dfinish, unsigned ifinish) {
  if (reset) {
    resets += owner[0].has_value() || owner[1].has_value();
    packets = {}; owner = {}; residency = {}; sequences.clear(); expected.clear(); cycle = 0;
    return;
  }
  for (unsigned lane = 0; lane < 2; ++lane) if (lane == 0 ? dfinish : ifinish) {
    if (!residency[lane]) fail("completion without refill residency");
    expected.record_end(*residency[lane], cycle);
    residency[lane].reset(); ++completed_refills;
  }
  if (command) {
    if (owner[0] || owner[1] || residency[0] || residency[1]) fail("new command overwrote line ownership");
    packets = {}; rom = instruction_rom; roms += rom;
    residency[0] = begin_refill(test_sites::dcache_refill, 0x1000);
    residency[1] = begin_refill(test_sites::icache_refill, rom ? 0x8000 : 0x1000);
  }
  sample_lane(0, dfire, dpacket, dack, ddbid);
  sample_lane(1, ifire, ipacket, iack, idbid);
  stalls += stalled; ++cycle;
}
extern "C" void compack_check() {
  if (rheg::graph().json() != expected.json()) fail("CompAck exact occurrence graph mismatch");
}
extern "C" void compack_finish() {
  rheg::graph().validate();
  if (owner[0] || owner[1] || residency[0] || residency[1] || completed_refills != 24 || acks < 12 || roms < 3 || stalls < 20 || resets < 2 || nonmax_final < 8)
    fail("CompAck trace coverage incomplete");
  std::printf("CompAck ancestry passed: %u acknowledgements, %u ROM reads, %u stalled cycles, %u pending resets, %u nonmaximum final packets\n",
      acks, roms, stalls, resets, nonmax_final);
}
