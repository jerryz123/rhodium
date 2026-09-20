// Checks copyback residency boundaries against public command/completion handshakes.
// SPDX-License-Identifier: Apache-2.0
#include "../../../../../rheg/runtime/rheg.h"
#include "rv5stage-copyback_manifest.h"
#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
std::array<std::optional<unsigned>,3> sites;
std::array<std::optional<rheg::Ref>,3> owners;
std::map<rheg::Ref,std::uint64_t> ends;
std::uint64_t cycle=0;
unsigned starts=0, finishes=0, children=0, count=0, closed=0, emitted=0, transfers=0;
bool resetting=true;
[[noreturn]] void fail(const char* message) { std::fprintf(stderr,"copyback residency: %s at cycle %llu\n",message,(unsigned long long)cycle); std::abort(); }
}
extern "C" void copyback_bind() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
extern "C" void copyback_sample(unsigned reset, unsigned admitted, unsigned completed, unsigned requests, unsigned packets) {
  resetting=reset; starts=admitted; finishes=completed; children=requests|packets;
}
extern "C" void copyback_check() {
  auto& graph=rheg::graph(); graph.validate();
  if(resetting) { owners={}; ends.clear(); cycle=count=closed=emitted=transfers=0; return; }
  for(unsigned lane=0; lane<3; ++lane) if(finishes & (1u<<lane)) {
    if(!owners[lane]) fail("completion without owner");
    ends.emplace(*owners[lane],cycle); owners[lane].reset(); ++closed;
  }
  unsigned observed_starts=0, observed_children=0;
  for(const auto& [ref,node]:graph.nodes) {
    const bool residency=rheg_generated::manifest().residency_sites.count(ref.site);
    if(node.cycle==cycle && !residency) {
      if(!children || (children & (children-1))) fail("unexpected or ambiguous child transfer");
      unsigned lane=0; while(!(children & (1u<<lane))) ++lane;
      if(!owners[lane] || !graph.edges.count({*owners[lane],ref}) || node.ancestry_unknown) fail("lost request/data residency parent");
      unsigned parents=0;
      for(const auto& edge:graph.edges) if(!(edge.second<ref) && !(ref<edge.second)) ++parents;
      if(parents!=1) fail("ambiguous child ancestry");
      ++observed_children; ++transfers; ++emitted;
    }
    if(node.cycle==cycle && residency) {
      if(!starts || (starts & (starts-1))) fail("unexpected or ambiguous admission");
      unsigned lane=0; while(!(starts & (1u<<lane))) ++lane;
      if(owners[lane] || (sites[lane] && *sites[lane]!=ref.site)) fail("overwritten or changed lane owner");
      for(unsigned other=0; other<3; ++other) if(other!=lane && sites[other]==ref.site) fail("lanes share occurrence site");
      sites[lane]=ref.site; owners[lane]=ref; ++observed_starts; ++count; ++emitted;
      if(graph.field(ref,"address").unsigned_value()!=0x80001240ULL) fail("address capture");
    }
    const auto end=ends.find(ref);
    if(node.end_cycle!=(end==ends.end() ? std::nullopt : std::optional<std::uint64_t>(end->second))) fail("wrong release cycle");
  }
  if(observed_starts!=unsigned(starts!=0) || observed_children!=unsigned(children!=0) || graph.nodes.size()!=emitted) fail("transfer count");
  ++cycle;
}
extern "C" void copyback_finish() {
  if(count!=15 || closed!=15 || transfers!=65 || !sites[0] || !sites[1] || !sites[2]) fail("incomplete residency coverage");
  std::puts("Copyback residency passed: 15 exact intervals and 65 request/data edges across retries and stalls");
}
