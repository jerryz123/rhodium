// Models public FIFO transfers through two buffered exits and a crossbar feedback lane.
#include "../../../rheg/runtime/rheg.h"
#include "event-branching_manifest.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <optional>
namespace {
struct Pending { rheg::Ref ref; unsigned payload, source, laps; };
struct Lane {
  std::array<std::deque<Pending>,3> inputs;
  std::array<std::deque<Pending>,2> outputs;
  std::array<std::uint64_t,4> sequences{};
};
struct Coverage {
  unsigned routes[3][3]{}, exits[2][2]{};
  unsigned laps=0, repeated=0, stalls=0, changes=0, concurrent=0, replacements=0, resets=0, last_grants=0;
};
std::array<Lane,2> lanes;
std::array<Coverage,2> coverage;
rheg::Graph expected;
std::uint64_t cycle=0;
bool resetting=true;
[[noreturn]] void fail(const char* message) {
  std::fprintf(stderr,"branching trace: %s at cycle %llu\n",message,(unsigned long long)cycle);
  std::abort();
}
Pending node(unsigned lane, unsigned local, unsigned payload) {
  rheg::Ref ref{branching_sites[lane][local],lanes[lane].sequences[local]++};
  expected.nodes[ref]={true,cycle,8,{{0,payload}}};
  return {ref,payload,local,0};
}
}
extern "C" void branching_bind() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
extern "C" void branching_sample(unsigned lane, unsigned reset, unsigned valid, unsigned ready, unsigned payloads,
    unsigned out_valid, unsigned out_ready, unsigned out_payloads, unsigned grants) {
  auto& state=lanes.at(lane); auto& c=coverage.at(lane); resetting=reset;
  if(reset) {
    bool pending=false;
    for(const auto& q:state.inputs) pending|=!q.empty();
    for(const auto& q:state.outputs) pending|=!q.empty();
    if(pending) ++c.resets;
    state=Lane{};
    if(lane==0) { expected.clear(); cycle=0; }
    return;
  }
  unsigned expected_ready=0, expected_valid=0;
  for(unsigned i=0;i<2;++i) {
    if(state.inputs[i].size()<3) expected_ready|=1u<<i;
    if(!state.outputs[i].empty()) expected_valid|=1u<<i;
  }
  if(ready!=expected_ready || out_valid!=expected_valid) fail("public handshakes differ from FIFO model");
  const std::array<bool,3> space{{state.outputs[0].size()<2,state.outputs[1].size()<2,state.inputs[2].size()<3}};
  std::array<int,3> selected{{-1,-1,-1}};
  std::array<bool,3> removed{};
  std::array<std::optional<Pending>,3> transferred;
  for(unsigned i=0;i<3;++i) {
    const auto row=(grants>>(3*i))&7;
    if(row && (row&(row-1))) fail("non-onehot stimulus row");
    for(unsigned o=0;o<3;++o) if(row&(1u<<o)) {
      if(selected[o]>=0) fail("non-onehot stimulus column");
      selected[o]=int(i);
      if(!state.inputs[i].empty() && space[o]) {
        transferred[o]=state.inputs[i].front(); removed[i]=true; ++c.routes[i][o];
      }
    }
  }
  if((out_valid & out_ready)==3) ++c.concurrent;
  for(unsigned o=0;o<2;++o) if(out_valid&(1u<<o)) {
    const auto parent=state.outputs[o].front();
    const auto payload=(out_payloads>>(8*o))&255;
    if(payload!=parent.payload) fail("public output payload differs");
    if(out_ready&(1u<<o)) {
      state.outputs[o].pop_front();
      const auto child=node(lane,2+o,payload);
      expected.edges.insert({parent.ref,child.ref}); ++c.exits[parent.source][o];
    } else {
      ++c.stalls;
      if(grants!=c.last_grants) ++c.changes;
    }
  }
  for(unsigned i=0;i<3;++i) if(removed[i]) state.inputs[i].pop_front();
  for(unsigned i=0;i<2;++i) if(valid & ready & (1u<<i)) {
    if(removed[i]) ++c.replacements;
    state.inputs[i].push_back(node(lane,i,(payloads>>(8*i))&255));
  }
  for(unsigned o=0;o<3;++o) if(transferred[o]) {
    auto parent=*transferred[o];
    if(o==2) {
      ++parent.laps; ++c.laps;
      if(parent.laps>=2) ++c.repeated;
      state.inputs[2].push_back(parent);
    } else state.outputs[o].push_back(parent);
  }
  c.last_grants=grants;
}
extern "C" void branching_check() {
  rheg::graph().validate();
  if(rheg::graph().json()!=expected.json()) fail("exact occurrence graph differs from transfer scoreboard");
  if(!resetting) ++cycle;
}
extern "C" void branching_finish() {
  for(unsigned lane=0;lane<2;++lane) {
    const auto& state=lanes[lane]; const auto& c=coverage[lane];
    for(const auto& q:state.inputs) if(!q.empty()) fail("input queue did not drain");
    for(const auto& q:state.outputs) if(!q.empty()) fail("exit queue did not drain");
    for(const auto& row:c.routes) for(auto count:row) if(!count) fail("missing crossbar route coverage");
    for(const auto& row:c.exits) for(auto count:row) if(!count) fail("missing source-to-exit coverage");
    if(!c.repeated || !c.stalls || !c.changes || !c.concurrent || !c.replacements || !c.resets)
      fail("missing repeated feedback, stall/grant-change, simultaneous transfer, or reset coverage");
    std::printf("Branching lane %u exact ancestry passed: %u feedback hops, %u repeated hops, %u stalls, %u grant changes, %u concurrent exits, %u simultaneous replacements, %u pending resets\n",
      lane,c.laps,c.repeated,c.stalls,c.changes,c.concurrent,c.replacements,c.resets);
  }
}
