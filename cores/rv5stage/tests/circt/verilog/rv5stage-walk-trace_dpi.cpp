// Reconstructs exact walk nodes, lifetime ends, and PTE/completion parents from public transfers.
// SPDX-License-Identifier: Apache-2.0
#include "../../../../../rheg/runtime/rheg.h"
#include "rv5stage-walk-trace_manifest.h"
#include <cstdio>
#include <cstdlib>

namespace {
rheg::Graph expected;
std::map<unsigned,std::uint64_t> sequences;
std::optional<rheg::Ref> owner;
std::uint64_t cycle=0;
unsigned started=0, finished=0, canceled=0, reset_pending=0, ptes=0, faults=0;
[[noreturn]] void fail(const char* message) { std::fprintf(stderr,"walk trace: %s at cycle %llu\n",message,(unsigned long long)cycle); std::abort(); }
rheg::Ref node(unsigned site, std::initializer_list<std::pair<const char*,std::uint64_t>> values) {
  const auto& manifest=rheg_generated::manifest();
  rheg::Ref ref{site,sequences[site]++};
  auto& n=expected.nodes[ref]; n.present=true; n.cycle=cycle; n.width=manifest.payload_widths.at(site);
  for(unsigned word=0; word<(n.width+31)/32; ++word) n.words[word]=0;
  for(const auto& field:manifest.fields.at(site)) {
    bool found=false;
    for(const auto& [name,value]:values) if(field.name==name) {
      found=true;
      for(unsigned bit=0; bit<field.width; ++bit)
        n.words[(field.offset+bit)/32] |= unsigned((value>>bit)&1) << ((field.offset+bit)%32);
    }
    if(!found) fail("unexpected capture field");
  }
  return ref;
}
}
extern "C" void walk_bind() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
extern "C" void walk_sample(unsigned reset, unsigned cancel, unsigned start, std::uint64_t address,
    unsigned request, std::uint64_t memory_address, unsigned finish, unsigned fault, unsigned access_fault) {
  if(reset) {
    reset_pending+=bool(owner); expected.clear(); sequences.clear(); owner.reset(); cycle=0; return;
  }
  if(start && !cancel) {
    if(owner) fail("admission overwrote owner");
    const auto parent=node(test_sites::translation,{{"address",address}});
    expected.nodes.at(parent).ancestry_unknown=true; // Scalar bench stimulus has no upstream occurrence.
    owner=node(test_sites::mmu_walk,{{"virtual_address",address},{"access",1},{"privilege",1}});
    expected.edges.insert({parent,*owner}); ++started;
  }
  if(request) {
    if(!owner) fail("PTE without owner");
    const auto child=node(test_sites::pte,{{"address",memory_address}});
    expected.edges.insert({*owner,child}); ++ptes;
  }
  if(finish && !cancel) {
    if(!owner) fail("completion without owner");
    const auto child=node(test_sites::completed,{{"fault",fault},{"access_fault",access_fault}});
    expected.edges.insert({*owner,child}); ++finished; faults+=fault || access_fault;
  }
  if(owner && (finish || cancel)) { expected.record_end(*owner,cycle); owner.reset(); canceled+=bool(cancel); }
  ++cycle;
}
extern "C" void walk_check() {
  rheg::graph().validate();
  if(rheg::graph().json()!=expected.json()) fail("exact residency graph mismatch");
}
extern "C" void walk_finish() {
  if(owner || started!=11 || finished!=7 || canceled!=3 || reset_pending!=1 || ptes<14 || faults!=3) fail("incomplete coverage");
  std::printf("Walker residency passed: %u starts, %u completions, %u cancellations, %u pending resets, %u PTEs\n",started,finished,canceled,reset_pending,ptes);
}
