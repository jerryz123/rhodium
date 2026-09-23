// Checks vector WB lineage and nonserializing vset issue timing.
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
  std::optional<std::uint64_t> first_configuration_cycle;
  std::optional<std::uint64_t> dependent_configuration_cycle;
  std::optional<std::uint64_t> configuration_cycle;
  std::optional<std::uint64_t> following_vector_cycle;
  std::optional<std::uint64_t> following_configuration_cycle;
  for(const auto& pair:graph.nodes) {
    if(pair.first.site==vector_core_sites::wb) {
      if(!writebacks.emplace(pair.second.cycle,pair.first).second) fail("multiple WB arrivals in one cycle");
      const auto pc=graph.field(pair.first,"pc").unsigned_value();
      if(pc==20 && !first_configuration_cycle) first_configuration_cycle=pair.second.cycle;
      if(pc==24 && !dependent_configuration_cycle) dependent_configuration_cycle=pair.second.cycle;
      if(pc==192 && !configuration_cycle) configuration_cycle=pair.second.cycle;
      if(pc==196 && !following_vector_cycle) following_vector_cycle=pair.second.cycle;
      if(pc==200 && !following_configuration_cycle) following_configuration_cycle=pair.second.cycle;
    }
  }
  const auto first_pair_adjacent=first_configuration_cycle && dependent_configuration_cycle &&
    *dependent_configuration_cycle==*first_configuration_cycle+1;
  const auto later_sequence_adjacent=configuration_cycle && following_vector_cycle &&
    following_configuration_cycle && *following_vector_cycle==*configuration_cycle+2 &&
    *following_configuration_cycle==*following_vector_cycle+2;
  if(!first_pair_adjacent || !later_sequence_adjacent) {
    std::fprintf(stderr,"core/vector trace: vset sequence cycles %llu/%llu and %llu/%llu/%llu\n",
                 static_cast<unsigned long long>(first_configuration_cycle.value_or(0)),
                 static_cast<unsigned long long>(dependent_configuration_cycle.value_or(0)),
                 static_cast<unsigned long long>(configuration_cycle.value_or(0)),
                 static_cast<unsigned long long>(following_vector_cycle.value_or(0)),
                 static_cast<unsigned long long>(following_configuration_cycle.value_or(0)));
    fail("vset added a WB gap beyond the surrounding fetch cadence");
  }
  std::map<rheg::Ref,unsigned> sequenced_by_wb;
  for(const auto& [ref,node]:graph.nodes) if(ref.site==vector_core_sites::sequence) {
    if(node.ancestry_unknown) fail("sequence has unknown WB ancestry");
    if(node.end_cycle) fail("sequence unexpectedly has a duration");
    unsigned parents=0;
    rheg::Ref parent{};
    for(const auto& edge:graph.edges) if(equal(edge.second,ref)) {
      if(edge.first.site!=vector_core_sites::wb) fail("sequence inherited a non-WB occurrence");
      parent=edge.first;
      ++parents;
    }
    if(parents!=1) fail("sequence must have exactly one WB parent");
    if(graph.nodes.at(parent).cycle>=node.cycle) fail("sequence did not follow its WB admission");
    if(graph.field(ref,"pc").unsigned_value()!=graph.field(parent,"pc").unsigned_value() ||
        graph.field(ref,"instruction").unsigned_value()!=graph.field(parent,"instruction").unsigned_value())
      fail("sequence snapshot differs from WB instruction");
    if(graph.field(parent,"instruction").unsigned_value()==0x00218057)
      fail("illegal masked-v0 instruction entered the sequencer");
    ++sequenced_by_wb[parent];
  }
  if(sequenced_by_wb.size()<8) fail("insufficient sequencing coverage");
  std::printf("Core/vector lineage passed: %zu sequenced instructions with WB ancestry\n",sequenced_by_wb.size());
}
