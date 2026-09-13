// Checks D-cache stage timing and retained ancestry against public core/cache transfers.
// SPDX-License-Identifier: Apache-2.0
#include "../../../rheg/runtime/rheg.h"
#include "rv5stage-load-hit_manifest.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>

namespace {
struct Demand { rheg::Ref parent; std::uint64_t address, cycle; };
std::deque<Demand> pending, advancing;
std::deque<rheg::Ref> launching;
std::map<rheg::Ref,rheg::Ref> accesses_by_mem;
std::uint64_t cycle=0, accesses=0, responses=0, lookups=0, resolutions=0;
std::uint64_t admissions=0, refills=0, rejected=0, hits=0, replays=0, cache_address=0;
bool resetting=true, accepted=false, cache_accepted=false;
bool tx_accepted=false;
std::uint64_t attempts=0, retries=0;
constexpr std::array<std::uint64_t, 6> miss_pcs{4, 68, 72, 76, 608, 640};
constexpr std::array<std::uint64_t, 6> miss_addresses{0x1000,0x1100,0x1200,0x1300,0x1400,0x1440};
[[noreturn]] void fail(const char* message) { std::fprintf(stderr,"dcache trace: %s at cycle %llu\n",message,(unsigned long long)cycle); std::abort(); }
bool equal(rheg::Ref a, rheg::Ref b) { return a.site==b.site && a.sequence==b.sequence; }
std::uint64_t field(rheg::Ref ref, const char* name) { return rheg::graph().field(ref,name).unsigned_value(); }
rheg::Ref parent_of(rheg::Ref child) {
  if(rheg::graph().nodes.at(child).ancestry_unknown) fail("incomplete demand ancestry");
  std::optional<rheg::Ref> result;
  for(const auto& edge:rheg::graph().edges) if(equal(edge.second,child)) {
    if(result) fail("multiple parents");
    result=edge.first;
  }
  if(!result) fail("missing parent");
  return *result;
}
void check_core_parent(rheg::Ref child, unsigned site) {
  auto parent=parent_of(child);
  if(parent.site!=site || rheg::graph().nodes.at(parent).cycle!=cycle ||
     rheg::graph().nodes.at(child).cycle!=cycle) fail("core stage alignment");
  for(const auto* name:{"pc","instruction"})
    if(field(parent,name)!=field(child,name)) fail("wrong core occurrence");
}
}
extern "C" void demand_init() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
extern "C" void demand_sample(unsigned reset, unsigned attempt, unsigned fire, unsigned cached, std::uint64_t address, unsigned txfire) {
  resetting=reset; accepted=fire; cache_accepted=cached; cache_address=address;
  tx_accepted=txfire;
  if(reset) {
    pending.clear(); advancing.clear(); accesses_by_mem.clear();
    launching.clear(); attempts=retries=0;
    cycle=accesses=responses=lookups=resolutions=admissions=refills=rejected=hits=replays=0;
    return;
  }
  if(attempt && !fire) ++rejected;
  if(cached && !fire) fail("cache accepted without a core demand");
}
extern "C" void demand_check(unsigned done) {
  auto& graph=rheg::graph();
  graph.validate();
  if(resetting) return;
  const rheg::Ref access{demand_sites::access,accesses};
  if(graph.nodes.count(access)) {
    check_core_parent(access,demand_sites::mem);
    accesses_by_mem.emplace(parent_of(access),access);
    ++accesses;
  }
  const rheg::Ref response{demand_sites::response,responses};
  if(graph.nodes.count(response)) {
    check_core_parent(response,demand_sites::wb);
    auto mem=parent_of(parent_of(response));
    auto prior=accesses_by_mem.find(mem);
    if(prior==accesses_by_mem.end()) fail("S2 has no corresponding S1");
    if(graph.nodes.at(prior->second).cycle+1!=cycle ||
       field(prior->second,"pc")!=field(response,"pc") ||
       field(prior->second,"address")!=field(response,"address")) fail("S1/S2 capture or latency");
    if(bool(field(response,"admitted"))!=accepted) fail("S2 admission differs from public transfer");
    if(accepted && (field(response,"fault") || field(response,"replay"))) fail("admitted fault or replay");
    if(accepted) ++admissions;
    if(field(response,"replay")) ++replays;
    if(field(response,"outcome")==1 || field(response,"outcome")==2) ++hits;
    if(cache_accepted) pending.push_back({response,cache_address,cycle});
    ++responses;
  } else if(accepted) fail("accepted request without S2");
  const rheg::Ref lookup{demand_sites::lookup,lookups};
  if(graph.nodes.count(lookup)) {
    if(pending.empty()) fail("lookup without an accepted cache request");
    auto request=pending.front(); pending.pop_front();
    if(!equal(parent_of(lookup),request.parent)) fail("cache queue lost S2 identity");
    if(graph.nodes.at(lookup).cycle!=cycle || cycle<request.cycle+1) fail("S3 latency");
    if(field(lookup,"address")!=request.address || field(lookup,"prefetch")) fail("lookup payload");
    advancing.push_back({lookup,request.address,cycle});
    ++lookups;
  }
  const rheg::Ref resolved{demand_sites::resolve,resolutions};
  if(graph.nodes.count(resolved)) {
    if(advancing.empty()) fail("S4 without S3");
    auto request=advancing.front(); advancing.pop_front();
    if(!equal(parent_of(resolved),request.parent)) fail("S4 lost S3 identity");
    if(graph.nodes.at(resolved).cycle!=cycle || cycle!=request.cycle+1) fail("S3/S4 latency");
    if(field(resolved,"address")!=request.address || field(resolved,"prefetch")) fail("resolution payload");
    if(field(resolved,"refill_accepted")) {
      if(refills>=miss_pcs.size()) fail("extra refill command");
      auto origin=parent_of(request.parent);
      if(origin.site!=demand_sites::response || field(origin,"pc")!=miss_pcs[refills]) fail("refill belongs to wrong instruction");
      if(field(resolved,"refill_address")!=miss_addresses[refills]) fail("refill address");
      // ReadUnique for the output-line and delayed store misses; ReadClean for loads.
      if(field(resolved,"refill_opcode")!=((refills==3 || refills==5) ? 0x07 : 0x02)) fail("refill opcode");
      launching.push_back(resolved);
      ++refills;
    }
    ++resolutions;
  }
  const rheg::Ref attempt{demand_sites::txreq,attempts};
  if(bool(graph.nodes.count(attempt))!=tx_accepted) fail("TXREQ differs from public handshake");
  if(tx_accepted) {
    if(launching.empty() || !equal(parent_of(attempt),launching.front())) fail("TXREQ lost retained S4 parent");
    auto origin=launching.front();
    if(graph.nodes.at(attempt).cycle!=cycle || cycle<=graph.nodes.at(origin).cycle) fail("TXREQ timestamp");
    if(field(attempt,"address")!=field(origin,"refill_address") ||
       field(attempt,"opcode")!=field(origin,"refill_opcode")) fail("TXREQ command capture");
    if(!field(attempt,"allow_retry")) { ++retries; launching.pop_front(); }
    ++attempts;
  }
  if(done) {
    if(!pending.empty() || !advancing.empty() || refills!=6 || resolutions!=6 || lookups!=6 ||
       !rejected || !hits || !replays || !launching.empty() || attempts!=12 || retries!=6) fail("missing drain, miss, hit, rejection, or retry coverage");
    std::printf("D-cache S1/S2 core alignment and S2 -> S3 -> S4 lineage passed (%llu responses, %llu admissions, %llu rejected attempts)\n",
                (unsigned long long)responses,(unsigned long long)admissions,(unsigned long long)rejected);
    std::printf("S4 -> CHI TXREQ retained lineage passed (12 attempts, 6 retries)\n");
  }
  ++cycle;
}
