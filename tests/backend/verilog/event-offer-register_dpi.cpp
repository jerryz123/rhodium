// Checks independent selected and nearest retained owners, including replacement and stalls.
// SPDX-License-Identifier: Apache-2.0
#include "../../../rheg/runtime/rheg.h"
#include "event-offer-register_manifest.h"
#include <cstdio>
#include <cstdlib>
#include <optional>
namespace {
rheg::Graph expected;
std::map<unsigned,std::uint64_t> sequences;
std::optional<rheg::Ref> owner;
std::optional<rheg::Ref> captured_owner;
std::uint64_t cycle=0;
unsigned payload=0,replaced=0,simultaneous=0,drained=0,stalled=0,pending_resets=0;
[[noreturn]] void fail(const char* why) { std::fprintf(stderr,"offer register cycle %llu: %s\n",(unsigned long long)cycle,why); std::abort(); }
rheg::Ref node(unsigned site,unsigned value,std::optional<rheg::Ref> parent={}) {
  rheg::Ref ref{site,sequences[site]++}; auto& n=expected.nodes[ref];
  n.present=true; n.cycle=cycle; n.width=8; n.words[0]=value;
  if(parent) expected.edges.insert({*parent,ref}); return ref;
}
}
extern "C" void offer_register_bind() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
extern "C" void offer_register_sample(unsigned reset,unsigned update,unsigned data,unsigned valid,unsigned ready,unsigned output) {
  if(reset) { pending_resets+=owner.has_value(); owner.reset(); captured_owner.reset(); expected.clear(); sequences.clear(); cycle=0; return; }
  if(bool(valid)!=owner.has_value() || (owner && output!=payload)) fail("functional offer mismatch");
  std::optional<rheg::Ref> incoming;
  std::optional<rheg::Ref> captured;
  if(update) { incoming=node(test_sites::update,data); captured=node(test_sites::captured,data,incoming); }
  if(owner) {
    if(ready) node(test_sites::delivered,payload,captured_owner);
    node(ready?test_sites::offer:test_sites::offer_stall,payload,owner);
    replaced+=update&&!ready; simultaneous+=update&&ready; drained+=!update&&ready; stalled+=!ready;
  }
  if(update) {owner=incoming;captured_owner=captured;payload=data;} else if(ready) {owner.reset();captured_owner.reset();}
  ++cycle;
}
extern "C" void offer_register_check() { if(rheg::graph().json()!=expected.json()) fail("occurrence graph mismatch"); }
extern "C" void offer_register_finish() {
  rheg::graph().validate();
  if(!replaced||!simultaneous||!drained||!stalled||!pending_resets) fail("incomplete replacement coverage");
  std::printf("OfferRegister exact ancestry passed: %u stalled replacements, %u simultaneous updates/deliveries, %u drains, %u stalls, %u pending resets\n",replaced,simultaneous,drained,stalled,pending_resets);
}
