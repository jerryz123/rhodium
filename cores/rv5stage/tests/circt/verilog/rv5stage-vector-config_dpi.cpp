// Checks that real-core vector launches inherit that cycle's exact scalar WB occurrence.
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
  unsigned launches=0;
  for(const auto& pair:graph.nodes) if(pair.first.site==vector_core_sites::launch) {
    auto wb=writebacks.find(pair.second.cycle);
    if(wb==writebacks.end() || pair.second.ancestry_unknown) fail("launch without known same-cycle WB");
    unsigned parents=0;
    for(const auto& edge:graph.edges) if(equal(edge.second,pair.first)) {
      if(!equal(edge.first,wb->second)) fail("launch inherited a different WB occurrence");
      ++parents;
    }
    if(parents!=1) fail("launch must have exactly one WB parent");
    if(graph.field(pair.first,"pc").unsigned_value()!=graph.field(wb->second,"pc").unsigned_value() ||
        graph.field(pair.first,"instruction").unsigned_value()!=graph.field(wb->second,"instruction").unsigned_value())
      fail("launch snapshot differs from WB instruction");
    ++launches;
  }
  if(launches<8) fail("insufficient macro launch coverage");
  std::printf("Core/vector lineage passed: %u exact WB-to-launch edges\n",launches);
}
