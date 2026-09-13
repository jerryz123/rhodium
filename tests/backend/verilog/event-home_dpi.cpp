// Checks inclusive Home response ownership against public requests, including repeated IDs and reset.
// SPDX-License-Identifier: Apache-2.0
#include "../../../rheg/runtime/rheg.h"
#include "event-home_manifest.h"
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace {
rheg::Graph expected;
std::uint64_t cycle = 0;
std::map<unsigned, std::uint64_t> sequences;
std::optional<rheg::Ref> owner;
bool missed = false;
unsigned hits = 0, misses = 0, responses = 0, stalled = 0, resets = 0;
[[noreturn]] void fail(const char* message) { std::fprintf(stderr, "%s\n", message); std::abort(); }
rheg::Ref node(unsigned site, unsigned width, unsigned __int128 payload, bool child) {
  rheg::Ref ref{site,sequences[site]++};
  auto& n = expected.nodes[ref]; n.present = true; n.cycle = cycle; n.width = width;
  for (unsigned i = 0; i < (width+31)/32; ++i) n.words[i] = std::uint32_t(payload >> (i*32));
  if (child) {
    if (!owner) fail("Home response lacks an accepted request");
    expected.edges.insert({*owner,ref});
  }
  return ref;
}
}
extern "C" void event_home_bind() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
extern "C" void event_home_sample(unsigned reset, unsigned request_fire, std::uint64_t address,
    unsigned request_opcode, unsigned request_txn, unsigned request_src,
    unsigned response_fire, unsigned response_opcode, unsigned response_txn, unsigned response_tgt,
    unsigned data_fire, unsigned data_opcode, unsigned data_txn, unsigned data_tgt, unsigned data_id,
    unsigned backing_fire, unsigned output_stalled) {
  using namespace test_sites;
  if (reset) { resets += owner.has_value(); expected.clear(); sequences.clear(); owner.reset(); cycle = 0; missed = false; return; }
  if (request_fire) {
    owner = node(home_request,70,(static_cast<unsigned __int128>(address)<<26) |
        (std::uint64_t(request_opcode)<<19) | (request_txn<<7) | request_src,false);
    missed = false;
  }
  missed |= backing_fire;
  if (response_fire) {
    node(home_response,24,(response_opcode<<19) | (response_txn<<7) | response_tgt,true);
    ++responses;
  }
  if (data_fire) {
    node(home_data,25,(data_opcode<<21) | (data_txn<<9) | (data_tgt<<2) | data_id,true);
    if (missed) ++misses; else ++hits;
  }
  stalled += output_stalled;
  ++cycle;
}
extern "C" void event_home_check() {
  if (rheg::graph().json() != expected.json()) fail("Home exact occurrence graph mismatch");
}
extern "C" void event_home_finish() {
  rheg::graph().validate();
  if (!hits || !misses || !responses || !stalled || !resets) fail("Home trace coverage incomplete");
  std::printf("Home request ownership passed: %u hit beats, %u miss beats, %u responses, %u stalled cycles, %u pending resets\n",hits,misses,responses,stalled,resets);
}
