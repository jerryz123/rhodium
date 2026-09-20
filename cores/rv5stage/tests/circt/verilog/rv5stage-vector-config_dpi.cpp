// Checks FIFO macro admission from scalar WB into sequencer residency after precheck.
// SPDX-License-Identifier: Apache-2.0
#include "../../../../../rheg/runtime/rheg.h"
#include "rv5stage-vector-config_manifest.h"
#include <cstdio>
#include <cstdlib>
#include <map>
namespace {
[[noreturn]] void fail(const char* message) {
  std::fprintf(stderr,"core/vector trace: %s\n",message); std::abort();
}
bool equal(rheg::Ref a, rheg::Ref b) { return a.site==b.site && a.sequence==b.sequence; }
}
extern "C" void vector_core_trace_bind() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
extern "C" void vector_core_trace_finish() {
  const auto& graph=rheg::graph();
  graph.validate();
  std::map<std::uint64_t,rheg::Ref> writebacks;
  for(const auto& pair:graph.nodes) if(pair.first.site==vector_core_sites::wb)
    if(!writebacks.emplace(pair.second.cycle,pair.first).second) fail("multiple WB arrivals in one cycle");
  std::map<std::uint64_t,rheg::Ref> admissions;
  for(const auto& pair:graph.nodes) if(pair.first.site==vector_core_sites::wb) {
    const auto instruction=graph.field(pair.first,"instruction").unsigned_value();
    // This program deliberately sends one illegal masked-v0 operation through
    // WB to trap. WB arrival alone is not vector admission.
    if((instruction&0x7f)==0x57 && ((instruction>>12)&7)!=7 && instruction!=0x00218057)
      admissions.emplace(pair.second.cycle,pair.first);
  }
  std::map<std::uint64_t,rheg::Ref> sequencing;
  for(const auto& pair:graph.nodes) if(pair.first.site==vector_core_sites::sequencer)
    sequencing.emplace(pair.second.cycle,pair.first);
  if(admissions.size()!=sequencing.size()) {
    std::fprintf(stderr,"WB candidates=%zu sequencer admissions=%zu\n",admissions.size(),sequencing.size());
    fail("macro admission/residency count mismatch");
  }
  unsigned launches=0;
  auto wb=admissions.begin();
  for(const auto& [cycle,ref]:sequencing) {
    const auto& node=graph.nodes.at(ref);
    if(wb==admissions.end() || node.ancestry_unknown || wb->first>=cycle) fail("sequencer without prior WB admission");
    if(!node.end_cycle || *node.end_cycle<=cycle) fail("sequencer lifetime not closed");
    unsigned parents=0;
    for(const auto& edge:graph.edges) if(equal(edge.second,ref)) {
      if(!equal(edge.first,wb->second)) fail("sequencer inherited a different WB occurrence");
      ++parents;
    }
    if(parents!=1) fail("sequencer must have exactly one WB parent");
    if(graph.field(ref,"pc").unsigned_value()!=graph.field(wb->second,"pc").unsigned_value() ||
        graph.field(ref,"instruction").unsigned_value()!=graph.field(wb->second,"instruction").unsigned_value())
      fail("sequencer snapshot differs from WB instruction");
    ++launches;
    ++wb;
  }
  if(launches<8) fail("insufficient sequencing coverage");
  std::printf("Core/vector lineage passed: %u exact WB-to-sequencer edges\n",launches);
}
