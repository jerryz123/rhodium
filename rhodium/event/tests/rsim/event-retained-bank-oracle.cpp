// Compares every bank occurrence, parent, and residency endpoint with public-control ownership.
// SPDX-License-Identifier: Apache-2.0
#include "../../../../rheg/runtime/rheg.h"
#include "test-sites.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
rheg::Graph expected;
std::map<unsigned,std::uint64_t> sequences;
std::array<std::optional<rheg::Ref>,3> owners;
std::array<unsigned,3> resident_sites;
std::array<unsigned,2> child_sites;
unsigned parent_site;
std::uint64_t cycle=0;
unsigned concurrent=0, replacements=0, dual_output=0, dual_release=0, resets=0;
bool resetting=false;
[[noreturn]] void fail(const char* message) {
  std::fprintf(stderr,"bank trace: %s at %llu\n",message,(unsigned long long)cycle); std::abort();
}
rheg::Ref node(unsigned id,unsigned payload) {
  rheg::Ref ref{id,sequences[id]++};
  expected.nodes[ref]={true,cycle,8,{{0,payload}}};
  return ref;
}
}
extern "C" void bank_bind() {
  rheg::graph().bind_manifest(rheg_generated::manifest());
  parent_site=test_sites::parent;
  resident_sites={test_sites::resident0,test_sites::resident1,test_sites::resident2};
  child_sites={test_sites::child0,test_sites::child1};
}
extern "C" void bank_sample(unsigned reset,unsigned capture,unsigned allocation,unsigned releases,
    unsigned payload,unsigned emit0,unsigned select0,unsigned emit1,unsigned select1) {
  resetting=reset;
  unsigned live=0;
  for(const auto& owner:owners) live+=owner.has_value();
  if(reset) {
    resets+=live>0; owners={}; expected.clear(); sequences.clear(); cycle=0; return;
  }
  concurrent+=live>1;
  dual_output+=emit0 && emit1 && select0!=select1;
  for(unsigned lane=0;lane<2;++lane) {
    unsigned emit=lane ? emit1 : emit0, selected=lane ? select1 : select0;
    if(emit) {
      if(!owners[selected]) fail("output without owner");
      expected.edges.insert({*owners[selected],node(child_sites[lane],0x2a)});
    }
  }
  unsigned released=0;
  for(unsigned i=0;i<3;++i) if((releases & (1u<<i)) && owners[i]) {
    ++released; replacements+=capture && allocation==i;
    expected.record_end(*owners[i],cycle); owners[i].reset();
  }
  dual_release+=released>1;
  if(capture) {
    if(owners[allocation]) fail("overwritten owner");
    const auto parent=node(parent_site,payload);
    owners[allocation]=node(resident_sites[allocation],payload);
    expected.edges.insert({parent,*owners[allocation]});
  }
}
extern "C" void bank_check() {
  rheg::graph().validate();
  if(rheg::graph().json()!=expected.json()) fail("exact graph mismatch");
  if(!resetting) ++cycle;
}
extern "C" void bank_finish() {
  if(!concurrent || !replacements || !dual_output || !dual_release || !resets) fail("incomplete coverage");
  std::printf("bank lineage passed: concurrent=%u replacements=%u dual-output=%u dual-release=%u resets=%u\n",
    concurrent,replacements,dual_output,dual_release,resets);
}
