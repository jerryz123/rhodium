// Checks I-cache TXREQ ownership against public fetch admission, lookup timing, and CHI transfers.
// SPDX-License-Identifier: Apache-2.0
#include "../../../rheg/runtime/rheg.h"
#include "rv5stage-fetch-throughput_manifest.h"
#include <cstdio>
#include <cstdlib>

namespace {
struct Attempt { rheg::Ref ref; std::uint64_t address; };
std::optional<Attempt> s1, s2, owner;
std::uint64_t cycle=0, requests=0, attempts=0, total_attempts=0, retries=0, canceled=0, retained_flushes=0;
unsigned flags=0, beats=0;
std::uint64_t address=0, tx_address=0;
bool acknowledged=false;
enum { Reset=1, Admit=2, Clear=4, Kill=8, Outcome=16, Replay=32, Tx=64, Retryable=128, Data=256, Ack=512 };
[[noreturn]] void fail(const char* message) {
  std::fprintf(stderr,"icache trace: %s at cycle %llu\n",message,(unsigned long long)cycle);
  std::abort();
}
bool equal(rheg::Ref a, rheg::Ref b) { return a.site==b.site && a.sequence==b.sequence; }
std::uint64_t field(rheg::Ref ref, const char* name) { return rheg::graph().field(ref,name).unsigned_value(); }
}
extern "C" void fetch_trace_init() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
extern "C" void fetch_trace_sample(unsigned sample, std::uint64_t request_address, std::uint64_t request_line) {
  flags=sample; address=request_address; tx_address=request_line;
}
extern "C" void fetch_trace_check(unsigned done) {
  auto& graph=rheg::graph();
  graph.validate();
  if(flags & Reset) {
    canceled+=bool(owner);
    s1.reset(); s2.reset(); owner.reset(); acknowledged=false; beats=0;
    cycle=requests=attempts=0;
    return;
  }
  std::optional<Attempt> incoming;
  rheg::Ref request{fetch_sites::request,requests};
  if(bool(graph.nodes.count(request))!=bool(flags & Admit)) fail("S0 differs from public admission");
  if(flags & Admit) {
    if(graph.nodes.at(request).cycle!=cycle || (field(request,"pc") & ~3ULL)!=address) fail("S0 timestamp or PC");
    if(!graph.nodes.at(request).ancestry_unknown) fail("unmodeled S0 source must report unknown ancestry");
    incoming=Attempt{request,address};
    ++requests;
  }
  if((flags & Outcome) && !(flags & Clear)) {
    if(!s2) fail("S2 outcome without an admitted lookup");
    // The first miss while idle owns the acquisition. Younger replay outcomes
    // cannot replace it, even when they name the same line or PC.
    if((flags & Replay) && !owner) owner=s2;
  }
  rheg::Ref tx{fetch_sites::txreq,attempts};
  if(bool(graph.nodes.count(tx))!=bool(flags & Tx)) fail("TXREQ differs from public handshake");
  if(flags & Tx) {
    if(!owner) fail("TXREQ without an accepted miss");
    unsigned parents=0;
    for(const auto& edge:graph.edges) if(equal(edge.second,tx)) {
      ++parents;
      if(!equal(edge.first,owner->ref)) fail("TXREQ belongs to wrong fetch occurrence");
    }
    if(parents!=1 || graph.nodes.at(tx).ancestry_unknown) fail("missing or incomplete TXREQ ancestry");
    if(graph.nodes.at(tx).cycle!=cycle || cycle<=graph.nodes.at(owner->ref).cycle+2) fail("TXREQ timestamp");
    if(tx_address!=(owner->address & ~63ULL) || field(tx,"address")!=tx_address ||
       bool(field(tx,"allow_retry"))!=bool(flags & Retryable)) fail("TXREQ payload");
    ++attempts; ++total_attempts;
    if(!(flags & Retryable)) ++retries;
  }
  if((flags & Clear) && owner) ++retained_flushes;
  if(flags & Data) ++beats;
  if(flags & Ack) acknowledged=true;
  if(beats==4 && acknowledged) { owner.reset(); beats=0; acknowledged=false; }
  s2=(flags & (Clear|Kill)) ? std::nullopt : s1;
  // A transferred redirect replaces the cleared S1 occurrence at this edge.
  s1=incoming;
  if(done) {
    if(owner || !retries || total_attempts<2*retries || !retained_flushes || !canceled) fail("missing retry, retained flush, pending reset, or drain coverage");
    std::printf("Frontend S0 -> I-cache TXREQ exact lineage passed (%llu attempts, %llu retries, %llu retained flushes, %llu pending resets)\n",
                (unsigned long long)total_attempts,(unsigned long long)retries,
                (unsigned long long)retained_flushes,(unsigned long long)canceled);
  }
  ++cycle;
}
