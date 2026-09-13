// Scores known, opaque, and unannotated contributors independently of generated trace state.
// SPDX-License-Identifier: Apache-2.0
#include "../../../rheg/runtime/rheg.h"
#include "event-partial_manifest.h"
#include <cstdio>
#include <cstdlib>
#include <deque>
namespace {
struct Ancestry { std::vector<rheg::Ref> parents; bool unknown=false, unannotated=false; };
std::deque<Ancestry> selected, joined;
std::deque<rheg::Ref> middle;
rheg::Graph expected;
std::map<unsigned,std::uint64_t> sequences;
std::uint64_t cycle=0;
unsigned known_count=0, opaque_count=0, unannotated_count=0, isolated_count=0, resets=0, stalls=0, after_count=0;
[[noreturn]] void fail(const char* message) { std::fprintf(stderr,"%s at %llu\n",message,(unsigned long long)cycle); std::abort(); }
rheg::Ref node(unsigned site, Ancestry ancestry={}) {
  rheg::Ref ref{site,sequences[site]++};
  expected.nodes[ref]={true,cycle,0,{},ancestry.unknown};
  for (auto parent: ancestry.parents) expected.edges.insert({parent,ref});
  return ref;
}
}
extern "C" void partial_bind() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
extern "C" void partial_sample(unsigned reset,unsigned inputs,unsigned choice,unsigned select_fire,
    unsigned join_fire,unsigned middle_fire,unsigned after_fire,unsigned isolated,unsigned stalled,unsigned middle_stalled,unsigned isolated_stalled) {
  if (reset) {
    resets += !selected.empty() || !joined.empty() || !middle.empty();
    selected.clear(); joined.clear(); middle.clear(); expected.clear(); sequences.clear(); cycle=0; return;
  }
  std::optional<rheg::Ref> known, buddy;
  if (inputs&1) known=node(test_sites::known);
  if (inputs&2) node(test_sites::before);
  if (inputs&8) buddy=node(test_sites::buddy);
  if (after_fire) {
    if (middle.empty()) fail("downstream checkpoint without prior event");
    node(test_sites::after,{{middle.front()},false}); middle.pop_front(); ++after_count;
  }
  if (middle_fire) {
    if (joined.empty()) fail("missing joined transaction");
    auto parents=joined.front(); joined.pop_front();
    if (parents.unannotated) ++unannotated_count;
    else if (parents.unknown) ++opaque_count;
    else ++known_count;
    middle.push_back(node(test_sites::middle,parents));
  }
  if (join_fire) {
    if (selected.empty() || !buddy) fail("join missing a public contributor");
    auto parents=selected.front(); selected.pop_front(); parents.parents.push_back(*buddy); joined.push_back(parents);
  }
  if (select_fire) {
    if (choice==0 && !known) fail("selected known input did not fire");
    selected.push_back({choice==0 ? std::vector<rheg::Ref>{*known} : std::vector<rheg::Ref>{},choice!=0,choice==2});
  }
  if (isolated) { node(test_sites::isolated,{{},true}); ++isolated_count; }
  if (middle_stalled) node(test_sites::middle_stall,{{},true});
  if (isolated_stalled) node(test_sites::isolated_stall,{{},true});
  stalls+=stalled; ++cycle;
}
extern "C" void partial_check() {
  if (rheg::graph().json()!=expected.json()) fail("partial occurrence/edge/completeness mismatch");
}
extern "C" void partial_finish() {
  rheg::graph().validate();
  if (!selected.empty() || !joined.empty() || !middle.empty() || !known_count || !opaque_count ||
      !unannotated_count || !isolated_count || !after_count || !stalls || !resets) fail("partial coverage incomplete");
  std::printf("Partial tracing passed: %u known joins, %u opaque joins, %u unannotated joins, %u isolated events, %u downstream events, %u stalls, %u pending resets\n",
    known_count,opaque_count,unannotated_count,isolated_count,after_count,stalls,resets);
}
