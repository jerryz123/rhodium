// Scores retained ownership, retries, replacement, reset, and detached traffic from public transfers.
#include "../../../rheg/runtime/rheg.h"
#include "event-retained_manifest.h"
#include <array>
#include <cstdio>
#include <cstdlib>
namespace {
rheg::Graph expected;
std::array<std::uint64_t,2> sequence{};
std::optional<rheg::Ref> owner;
std::uint64_t cycle=0;
unsigned outputs=0, repeats=0, detached=0, replacements=0, pending_resets=0, deliveries=0;
bool resetting=true;
[[noreturn]] void fail(const char* message) { std::fprintf(stderr,"retained trace: %s at %llu\n",message,(unsigned long long)cycle); std::abort(); }
rheg::Ref node(unsigned site, unsigned payload) {
  rheg::Ref ref{site,sequence[site]++};
  expected.nodes[ref]={true,cycle,8,{{0,payload}}};
  return ref;
}
}
extern "C" void retained_bind() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
extern "C" void retained_sample(unsigned reset, unsigned capture, unsigned release,
    unsigned source_bits, unsigned transfer, unsigned unrelated_transfer, unsigned output_bits) {
  resetting=reset;
  if(reset) {
    if(owner) ++pending_resets;
    owner.reset(); deliveries=0; expected.clear(); sequence={}; cycle=0; return;
  }
  if(transfer) {
    auto child=node(1,output_bits); ++outputs;
    if(unrelated_transfer) ++detached;
    else {
      if(!owner) fail("output without resident owner");
      expected.edges.insert({*owner,child});
      if(deliveries++) ++repeats;
    }
  }
  if(capture && release && owner) ++replacements;
  if(release) { owner.reset(); deliveries=0; }
  if(capture) { owner=node(0,source_bits); deliveries=0; }
}
extern "C" void retained_check() {
  rheg::graph().validate();
  if(rheg::graph().json()!=expected.json()) fail("graph differs from public transfer scoreboard");
  if(!resetting) ++cycle;
}
extern "C" void retained_finish() {
  if(!outputs || !repeats || !detached || !replacements || !pending_resets) fail("missing repeat, detach, replacement, or pending-reset coverage");
  std::printf("retained lineage passed: outputs=%u repeats=%u detached=%u replacements=%u pending-resets=%u\n",outputs,repeats,detached,replacements,pending_resets);
}
