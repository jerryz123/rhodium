// Checks exact crossbar occurrence ancestry using public grants and an independent FIFO model.
// SPDX-License-Identifier: Apache-2.0
#include "../../../rheg/runtime/rheg.h"
#include "event-crossbar_manifest.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <deque>
namespace {
struct Pending { rheg::Ref ref; unsigned payload; };
struct Lane {
  std::array<std::deque<Pending>,3> queues;
  std::array<std::uint64_t,5> sequences{};
  std::array<int,2> stalled_selection{{-1,-1}};
};
struct Coverage {
  unsigned routes[3][2]{};
  unsigned stalls=0, changes=0, blocked=0, simultaneous=0, pending_resets=0, bypass=0, replacement=0, empty_selected=0;
};
std::array<Lane,2> lanes;
std::array<Coverage,2> coverage;
rheg::Graph expected;
std::uint64_t cycle=0;
bool resetting=true;
[[noreturn]] void fail(const char* message) {
  std::fprintf(stderr,"crossbar trace: %s at cycle %llu\n",message,(unsigned long long)cycle);
  std::abort();
}
Pending node(unsigned lane, unsigned local, unsigned payload) {
  rheg::Ref ref{crossbar_sites[lane][local],lanes[lane].sequences[local]++};
  expected.nodes[ref]={true,cycle,8,{{0,payload}}};
  return {ref,payload};
}
}
extern "C" void crossbar_bind() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
extern "C" void crossbar_sample(unsigned lane, unsigned reset, unsigned valid, unsigned ready,
    unsigned payloads, unsigned out_valid, unsigned out_ready, unsigned out_payloads, unsigned grants) {
  auto& state=lanes.at(lane); auto& c=coverage.at(lane);
  resetting=reset;
  if(reset) {
    for(const auto& q:state.queues) if(!q.empty()) { ++c.pending_resets; break; }
    state=Lane{};
    if(lane==0) { expected.clear(); cycle=0; }
    return;
  }
  std::array<int,2> selected{{-1,-1}};
  std::array<bool,3> transfer{};
  unsigned expected_valid=0;
  for(unsigned i=0;i<3;++i) {
    const unsigned row=(grants>>(2*i))&3;
    if(row==3) fail("illegal grant row in stimulus");
    for(unsigned o=0;o<2;++o) if(row&(1u<<o)) {
      if(selected[o]>=0) fail("illegal grant column in stimulus");
      selected[o]=int(i);
      if(!state.queues[i].empty() || (valid&(1u<<i))) expected_valid|=1u<<o;
      else ++c.empty_selected;
      transfer[i]=(expected_valid & out_ready & (1u<<o))!=0;
    }
  }
  if(out_valid!=expected_valid) fail("output validity disagrees with queue/grant model");
  if(!grants) ++c.blocked;
  if((out_valid & out_ready)==3) ++c.simultaneous;
  for(unsigned i=0;i<3;++i) {
    const bool can_accept=state.queues[i].size()<2 || transfer[i];
    if(bool(ready&(1u<<i))!=can_accept) fail("input readiness disagrees with FIFO model");
    if(valid & ready & (1u<<i)) {
      if(transfer[i]) {
        if(state.queues[i].empty()) ++c.bypass;
        if(state.queues[i].size()==2) ++c.replacement;
      }
      state.queues[i].push_back(node(lane,i,(payloads>>(8*i))&255));
    }
  }
  for(unsigned o=0;o<2;++o) {
    const bool stalled=(out_valid&(1u<<o)) && !(out_ready&(1u<<o));
    if(stalled) {
      ++c.stalls;
      if(state.stalled_selection[o]>=0 && state.stalled_selection[o]!=selected[o]) ++c.changes;
    }
    state.stalled_selection[o]=stalled ? selected[o] : -1;
    if(out_valid&(1u<<o)) {
      if(selected[o]<0 || state.queues[selected[o]].empty()) fail("offered output lacks owner");
      const auto parent=state.queues[selected[o]].front();
      const unsigned payload=(out_payloads>>(8*o))&255;
      if(payload!=parent.payload) fail("selected payload mismatch");
      if(out_ready&(1u<<o)) {
        state.queues[selected[o]].pop_front();
        const auto child=node(lane,3+o,payload);
        expected.edges.insert({parent.ref,child.ref});
        ++c.routes[selected[o]][o];
      }
    }
  }
}
extern "C" void crossbar_check() {
  rheg::graph().validate();
  if(rheg::graph().json()!=expected.json()) fail("graph differs from public-transfer occurrence model");
  if(!resetting) ++cycle;
}
extern "C" void crossbar_finish() {
  for(unsigned lane=0;lane<2;++lane) {
    const auto& c=coverage[lane];
    for(const auto& q:lanes[lane].queues) if(!q.empty()) fail("pending inputs did not drain");
    for(const auto& row:c.routes) for(unsigned count:row) if(count<8) fail("insufficient route coverage");
    std::fprintf(stderr,"crossbar lane %u coverage: stalls=%u changes=%u zero-grants=%u concurrent=%u resets=%u bypass=%u replacements=%u empty-selected=%u\n",
      lane,c.stalls,c.changes,c.blocked,c.simultaneous,c.pending_resets,c.bypass,c.replacement,c.empty_selected);
    if(!c.stalls || !c.changes || !c.blocked || !c.simultaneous || !c.pending_resets || !c.bypass || !c.replacement || !c.empty_selected)
      fail("missing stall, grant-change, zero-grant, concurrent-output, reset, FIFO, or empty-input coverage");
    std::printf("crossbar lane %u exact occurrence lineage passed\n",lane);
  }
}
