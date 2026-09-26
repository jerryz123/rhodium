// Checks cache SRAM-port events, resolution, and instruction/result ownership through caller storage.
// SPDX-License-Identifier: Apache-2.0
#include "../../../../../rheg/runtime/rheg.h"
#include "rv5stage-load-hit_manifest.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>

namespace {
struct Demand { rheg::Ref parent; std::uint64_t address, cycle; };
std::deque<Demand> pending, advancing;
std::deque<rheg::Ref> launching;
std::map<rheg::Ref,rheg::Ref> writebacks_by_parent;
std::uint64_t writebacks=0;
std::uint64_t cycle=0, tag_ports=0, memory_ports=0, accesses=0, responses=0, lookups=0, resolutions=0;
std::uint64_t admissions=0, refills=0, rejected=0, hits=0, replays=0, cache_address=0;
bool resetting=true, accepted=false, cache_accepted=false;
bool tx_accepted=false;
std::uint64_t attempts=0, retries=0;
constexpr std::array<std::uint64_t, 6> miss_pcs{4, 68, 72, 76, 608, 640};
constexpr std::array<std::uint64_t, 6> miss_addresses{0x1000,0x1100,0x1200,0x1300,0x1400,0x1440};
[[noreturn]] void fail(const char* message) { std::fprintf(stderr,"dcache trace: %s at cycle %llu\n",message,(unsigned long long)cycle); std::abort(); }
bool equal(rheg::Ref a, rheg::Ref b) { return a.site==b.site && a.sequence==b.sequence; }
std::uint64_t field(rheg::Ref ref, const char* name) { return rheg::graph().field(ref,name).unsigned_value(); }
rheg::Ref parent_of(rheg::Ref child, std::optional<unsigned> site=std::nullopt) {
  if(rheg::graph().nodes.at(child).ancestry_unknown) fail("incomplete demand ancestry");
  std::optional<rheg::Ref> result;
  for(const auto& edge:rheg::graph().edges) if(equal(edge.second,child) && (!site || edge.first.site==*site)) {
    if(result) fail("multiple parents");
    result=edge.first;
  }
  if(!result) fail("missing parent");
  return *result;
}
void check_core_parent(rheg::Ref child, unsigned site, unsigned delay=0) {
  auto parent=parent_of(child,site);
  if(parent.site!=site || rheg::graph().nodes.at(parent).cycle+delay!=cycle ||
     rheg::graph().nodes.at(child).cycle!=cycle) fail("core stage alignment");
  for(const auto* name:{"pc","instruction"})
    if(field(parent,name)!=field(child,name)) fail("wrong core occurrence");
}
}
extern "C" void demand_init() {
  rheg::graph().bind_manifest(rheg_generated::manifest());
  rheg::graph().bind_timing({100000000});
}
extern "C" void demand_sample(unsigned reset, unsigned attempt, unsigned fire, unsigned cached, std::uint64_t address, unsigned txfire) {
  resetting=reset; accepted=fire; cache_accepted=cached; cache_address=address;
  tx_accepted=txfire;
  if(reset) {
    pending.clear(); advancing.clear(); writebacks_by_parent.clear();
    writebacks=0;
    launching.clear(); attempts=retries=0;
    cycle=tag_ports=memory_ports=accesses=responses=lookups=resolutions=admissions=refills=rejected=hits=replays=0;
    return;
  }
  if(attempt && !fire) ++rejected;
  if(cached && !fire) fail("cache accepted without a core demand");
}
extern "C" void demand_check(unsigned done) {
  auto& graph=rheg::graph();
  graph.validate();
  if(resetting) return;
  const rheg::Ref wb{demand_sites::wb,writebacks};
  if(graph.nodes.count(wb)) {
    auto parent=parent_of(wb,demand_sites::mem);
    check_core_parent(wb,demand_sites::mem,1);
    for(const auto& edge:graph.edges) if(equal(edge.second,wb) && edge.first.site!=demand_sites::mem) {
      if(edge.first.site==demand_sites::ex) {
        if(!equal(edge.first,parent_of(parent))) fail("local LSU result belongs to a different instruction");
      } else if(edge.first.site!=demand_sites::access || graph.nodes.at(edge.first).cycle+1!=cycle ||
                !equal(parent_of(edge.first),parent_of(parent))) fail("WB paired the wrong cache response");
    }
    if(!writebacks_by_parent.emplace(parent,wb).second) fail("duplicate WB for predecessor");
    ++writebacks;
  }
  const rheg::Ref tags{demand_sites::tags,tag_ports};
  if(graph.nodes.count(tags)) {
    if(graph.nodes.at(tags).cycle!=cycle || field(tags,"write")!=(field(tags,"owner")==3))
      fail("tag port operation does not match its owner");
    ++tag_ports;
  }
  const rheg::Ref memory{demand_sites::memory,memory_ports};
  if(graph.nodes.count(memory)) {
    if(graph.nodes.at(memory).cycle!=cycle ||
       field(memory,"write")!=((field(memory,"owner")==3) || (field(memory,"owner")==4) || (field(memory,"owner")==6)))
      fail("data port operation does not match its owner");
    ++memory_ports;
  }
  const rheg::Ref access{demand_sites::access,accesses};
  if(graph.nodes.count(access)) {
    auto origin=parent_of(access);
    if(origin.site!=demand_sites::ex || graph.nodes.at(origin).cycle+1!=cycle ||
       graph.nodes.at(access).cycle!=cycle) fail("cache resolution is not aligned to EX plus one");
    ++accesses;
  }
  const rheg::Ref response{demand_sites::response,responses};
  if(graph.nodes.count(response)) {
    check_core_parent(response,demand_sites::mem,1);
    auto prior=parent_of(response,demand_sites::mem);
    auto wb_sibling=writebacks_by_parent.find(prior);
    if(field(response,"fault") || field(response,"replay")) {
      if(wb_sibling!=writebacks_by_parent.end()) fail("failed memory attempt retired");
    } else {
      if(wb_sibling==writebacks_by_parent.end() || graph.nodes.at(wb_sibling->second).cycle!=cycle)
        fail("successful memory result has no same-cycle retirement");
      for(const auto* name:{"pc","instruction"})
        if(field(wb_sibling->second,name)!=field(response,name)) fail("result/WB capture mismatch");
    }
    if(field(response,"outcome")==1 || field(response,"outcome")==2) {
      auto cache=parent_of(response,demand_sites::access);
      if(field(cache,"address")!=field(response,"address") || field(cache,"outcome")!=field(response,"outcome"))
        fail("cache hit capture mismatch");
    }
    if(bool(field(response,"admitted"))!=accepted) fail("result admission differs from public transfer");
    if(accepted && (field(response,"fault") || field(response,"replay"))) fail("admitted fault or replay");
    if(accepted) ++admissions;
    if(field(response,"replay")) ++replays;
    if(field(response,"outcome")==1 || field(response,"outcome")==2) ++hits;
    if(cache_accepted) pending.push_back({response,cache_address,cycle});
    ++responses;
  } else if(accepted) fail("accepted request without captured result");
  const rheg::Ref lookup{demand_sites::lookup,lookups};
  if(graph.nodes.count(lookup)) {
    if(pending.empty()) fail("lookup without an accepted cache request");
    auto request=pending.front(); pending.pop_front();
    if(!equal(parent_of(lookup),request.parent)) fail("cache queue lost caller result identity");
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
      const rheg::Ref resident{demand_sites::refill,refills};
      if(!graph.nodes.count(resident) || !equal(parent_of(resident),resolved) || graph.nodes.at(resident).cycle!=cycle)
        fail("refill residency lost S4 admission");
      if(field(resident,"address")!=field(resolved,"refill_address") || field(resident,"opcode")!=field(resolved,"refill_opcode"))
        fail("refill residency capture");
      launching.push_back(resident);
      ++refills;
    }
    ++resolutions;
  }
  const rheg::Ref attempt{demand_sites::txreq,attempts};
  if(bool(graph.nodes.count(attempt))!=tx_accepted) fail("TXREQ differs from public handshake");
  if(tx_accepted) {
    if(launching.empty() || !equal(parent_of(attempt),launching.front())) fail("TXREQ lost refill residency parent");
    auto origin=launching.front();
    if(graph.nodes.at(attempt).cycle!=cycle || cycle<=graph.nodes.at(origin).cycle) fail("TXREQ timestamp");
    if(field(attempt,"address")!=field(origin,"address") ||
       field(attempt,"opcode")!=field(origin,"opcode")) fail("TXREQ command capture");
    if(!field(attempt,"allow_retry")) { ++retries; launching.pop_front(); }
    ++attempts;
  }
  if(done) {
    if(!pending.empty() || !advancing.empty() || !tag_ports || !memory_ports || refills!=6 || resolutions!=6 || lookups!=6 ||
       !rejected || !hits || !replays || !launching.empty() || attempts!=12 || retries!=6) fail("missing drain, miss, hit, rejection, or retry coverage");
    std::printf("Shared cache -> WB and caller result -> S3 -> S4 lineage passed (%llu responses, %llu admissions, %llu rejected attempts)\n",
                (unsigned long long)responses,(unsigned long long)admissions,(unsigned long long)rejected);
    for(std::uint64_t sequence=0; sequence<refills; ++sequence)
      if(!graph.nodes.at({demand_sites::refill,sequence}).end_cycle) fail("refill did not release");
    std::printf("S4 -> refill residency -> CHI TXREQ lineage passed (12 attempts, 6 retries)\n");
    if(const auto* path=std::getenv("RHEG_LOAD_HIT_SNAPSHOT")) {
      std::ofstream file(path); file << graph.snapshot().json();
      if(!file) fail("cannot write load-hit snapshot");
    }
  }
  ++cycle;
}
