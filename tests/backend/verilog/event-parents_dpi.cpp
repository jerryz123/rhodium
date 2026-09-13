// Independently reconstructs exact root, intermediate, selected, and combined occurrence ancestry.
// SPDX-License-Identifier: Apache-2.0
#include "../../../rheg/runtime/rheg.h"
#include "event-parents_manifest.h"
#include <cstdio>
#include <cstdlib>
#include <optional>
namespace {
rheg::Graph expected;
std::map<unsigned,std::uint64_t> sequences;
struct Token { rheg::Ref root; std::optional<rheg::Ref> middle; unsigned data; };
std::optional<Token> pending;
std::uint64_t cycle=0;
unsigned payload=0,memory=0,bypass=0,clears=0,resets=0;
bool payload_known=false;
[[noreturn]] void fail(const char* why) { std::fprintf(stderr,"selected parents cycle %llu: %s\n",(unsigned long long)cycle,why); std::abort(); }
rheg::Ref node(unsigned site,unsigned data,std::initializer_list<rheg::Ref> parents={}) {
  rheg::Ref ref{site,sequences[site]++}; auto& n=expected.nodes[ref];
  n.present=true; n.cycle=cycle; n.width=8; n.words[0]=data;
  for(auto p:parents) expected.edges.insert({p,ref});
  return ref;
}
}
extern "C" void parents_bind() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
extern "C" void parents_sample(unsigned reset,unsigned flush,unsigned valid,unsigned data,unsigned wb_valid,unsigned wb_data,unsigned cache_valid,unsigned both_valid) {
  if(reset) { ++resets; pending.reset(); expected.clear(); sequences.clear(); cycle=0; payload_known=false; return; }
  if(bool(wb_valid)!=pending.has_value() || (payload_known && wb_data!=payload)) fail("functional WB mismatch including inactive payload");
  bool is_memory=pending && pending->middle.has_value();
  if(bool(cache_valid)!=pending.has_value() || bool(both_valid)!=pending.has_value()) fail("qualification changed functional validity");
  if(pending) {
    node(test_sites::wb,pending->data,{pending->root});
    if(is_memory) {
      node(test_sites::cache,pending->data,{*pending->middle});
      node(test_sites::both,pending->data,{pending->root,*pending->middle}); ++memory;
    } else ++bypass;
  }
  pending.reset();
  if(valid) {
    auto root=node(test_sites::root,data);
    std::optional<rheg::Ref> middle;
    if(data&1) middle=node(test_sites::middle,data,{root});
    if(!flush) pending=Token{root,middle,data}; else ++clears;
  }
  payload=data; payload_known=true; ++cycle;
}
extern "C" void parents_check() { if(rheg::graph().json()!=expected.json()) fail("exact graph mismatch"); }
extern "C" void parents_finish() {
  rheg::graph().validate();
  if(!memory || !bypass || !clears || resets<2) fail("incomplete path/reset coverage");
  std::printf("Selected parents passed: %u memory, %u bypass, %u flushed arrivals, %u reset cycles\n",memory,bypass,clears,resets);
}
