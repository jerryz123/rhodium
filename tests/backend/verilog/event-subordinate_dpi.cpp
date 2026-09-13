// Checks retained MMIO ancestry using only accepted public transfers and response opcodes.
// SPDX-License-Identifier: Apache-2.0
#include "../../../rheg/runtime/rheg.h"
#include "event-subordinate_manifest.h"
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace {
rheg::Graph expected;
std::optional<rheg::Ref> owner;
std::map<unsigned, std::uint64_t> sequences;
std::uint64_t cycle = 0;
unsigned reads = 0, writes = 0, dbids = 0, credits = 0, stalls = 0, resets = 0;
bool saw_dbid = false, saw_write_data = false;
[[noreturn]] void fail(const char* message) { std::fprintf(stderr, "%s\n", message); std::abort(); }
rheg::Ref node(unsigned site, unsigned txn, bool child) {
  rheg::Ref ref{site, sequences[site]++};
  auto& n = expected.nodes[ref];
  n.present = true; n.cycle = cycle; n.width = 12; n.words[0] = txn;
  if (child) {
    if (!owner) fail("subordinate response without an accepted request");
    expected.edges.insert({*owner, ref});
  }
  return ref;
}
}
extern "C" void event_subordinate_bind() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
extern "C" void event_subordinate_sample(unsigned reset, unsigned req_fire, unsigned req_opcode, unsigned req_txn,
    unsigned rsp_fire, unsigned rsp_opcode, unsigned rsp_txn, unsigned dat_fire, unsigned dat_txn,
    unsigned write_fire, unsigned write_opcode, unsigned stalled) {
  if (reset) {
    resets += owner.has_value(); owner.reset(); sequences.clear(); expected.clear(); cycle = 0;
    saw_dbid = saw_write_data = false;
    return;
  }
  if (req_fire && req_opcode != 0) {
    if (owner) fail("overlapping subordinate requests");
    owner = node(test_sites::request, req_txn, false);
    saw_dbid = saw_write_data = false;
  }
  if (req_fire && req_opcode == 0) ++credits;
  if (write_fire && write_opcode != 0) {
    if (!owner || !saw_dbid) fail("write data lost its request/DBID ownership");
    saw_write_data = true;
  }
  if (rsp_fire) {
    node(test_sites::response, rsp_txn, true);
    if (rsp_opcode == 6) { ++dbids; saw_dbid = true; }
    else if (rsp_opcode == 4) {
      if (!saw_write_data) fail("write completion preceded data");
      ++writes; owner.reset();
    } else fail("unexpected subordinate RSP opcode");
  }
  if (dat_fire) { node(test_sites::data, dat_txn, true); ++reads; owner.reset(); }
  stalls += stalled;
  ++cycle;
}
extern "C" void event_subordinate_check() {
  if (rheg::graph().json() != expected.json()) fail("subordinate exact occurrence graph mismatch");
}
extern "C" void event_subordinate_finish() {
  rheg::graph().validate();
  if (owner || reads < 4 || writes < 4 || dbids < 4 || credits < 8 || stalls < 8 || resets < 4)
    fail("subordinate trace coverage incomplete");
  std::printf("Subordinate ownership passed: %u reads, %u writes, %u DBIDs, %u credit returns, %u stalled cycles, %u pending resets\n",
      reads, writes, dbids, credits, stalls, resets);
}
