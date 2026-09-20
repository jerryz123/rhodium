// Checks ordinary S0 and fallback S2 ownership through the production prediction and cursor paths.
// SPDX-License-Identifier: Apache-2.0
#include "../../../../../rheg/runtime/rheg.h"
#include "rv5stage-fetch-prediction_manifest.h"
#include <cstdio>
#include <cstdlib>
#include <optional>

namespace {
struct Expectation {
  std::uint64_t source_word, target_pc, minimum_delay;
  std::optional<rheg::Ref> parent;
};
std::optional<Expectation> expected;
struct Attempt { std::uint64_t address; rheg::Ref ref; };
std::optional<Attempt> s1, s2;
std::uint64_t cycle=0, requests=0, outcomes=0, address=0;
unsigned sample=0, checked=0, held=0, successors=0;
enum { Reset=1, Flush=2, Kill=4, Fire=8, Response=16, Fault=32 };
[[noreturn]] void fail(const char* reason) {
  std::fprintf(stderr,"fallback trace cycle %llu: %s\n",(unsigned long long)cycle,reason);
  if(expected) std::fprintf(stderr,"expected source word %llx -> target PC %llx\n",
      (unsigned long long)expected->source_word,(unsigned long long)expected->target_pc);
  std::abort();
}
bool equal(rheg::Ref a, rheg::Ref b) { return a.site==b.site && a.sequence==b.sequence; }
void check_parent(rheg::Ref child, rheg::Ref parent) {
  const auto& graph=rheg::graph();
  unsigned parents=0;
  for(const auto& edge:graph.edges) if(equal(edge.second,child)) {
    ++parents;
    if(!equal(edge.first,parent)) fail("request inherited the wrong occurrence");
  }
  if(parents!=1 || graph.nodes.at(child).ancestry_unknown) fail("missing or incomplete request ancestry");
}
}
extern "C" void fallback_trace_bind() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
extern "C" void fallback_trace_expect(std::uint64_t source_word, std::uint64_t target_pc, std::uint64_t minimum_delay) {
  if(expected) fail("previous fallback expectation did not complete");
  expected=Expectation{source_word,target_pc,minimum_delay,{}};
}
extern "C" void fallback_trace_sample(unsigned reset, unsigned flush, unsigned kill, unsigned fire,
    unsigned response_valid, unsigned fault, std::uint64_t request_address) {
  sample=(reset?Reset:0)|(flush?Flush:0)|(kill?Kill:0)|(fire?Fire:0)|(response_valid?Response:0)|(fault?Fault:0);
  address=request_address;
}
extern "C" void fallback_trace_check() {
  auto& graph=rheg::graph();
  if(sample & Reset) {
    if(expected) fail("fallback expectation was lost across reset");
    s1.reset(); s2.reset(); cycle=requests=outcomes=0;
    return;
  }
  // Public request acceptance and S1 cancellation identify the exact S2 slot.
  // Repeated addresses never select a parent from the graph's edge list.
  const rheg::Ref outcome{test_sites::frontend_s2_outcome,outcomes};
  if(bool(graph.nodes.count(outcome))!=s2.has_value()) fail("S2 differs from public pipeline timing");
  if(s2) {
    if(graph.nodes.at(outcome).cycle!=cycle || (graph.field(outcome,"pc").unsigned_value() & ~3ULL)!=s2->address)
      fail("S2 timestamp or word address mismatch");
    if(expected && !expected->parent && s2->address==expected->source_word &&
        (sample & Response) && !(sample & (Flush|Fault))) expected->parent=outcome;
    ++outcomes;
  }
  const rheg::Ref request{test_sites::frontend_s0_request,requests};
  if(bool(graph.nodes.count(request))!=bool(sample & Fire)) fail("S0 differs from public acceptance");
  if(sample & Fire) {
    const auto pc=graph.field(request,"pc").unsigned_value();
    if(graph.nodes.at(request).cycle!=cycle || (pc & ~3ULL)!=address) fail("S0 timestamp or address mismatch");
    // With neither recovery nor late redirect/replay, a live S1 chooses the
    // ordinary successor (sequential or BTB-predicted) from its S0 occurrence.
    if(s1 && !(sample & (Flush|Kill))) { check_parent(request,s1->ref); ++successors; }
    if(expected && expected->parent && pc==expected->target_pc) {
      check_parent(request,*expected->parent);
      if(cycle-graph.nodes.at(*expected->parent).cycle<expected->minimum_delay) {
        std::fprintf(stderr,"delay=%llu minimum=%llu checked=%u\n",
            (unsigned long long)(cycle-graph.nodes.at(*expected->parent).cycle),
            (unsigned long long)expected->minimum_delay,checked);
        fail("fallback did not exercise required delay");
      }
      held+=expected->minimum_delay>1;
      ++checked;
      expected.reset();
    }
    ++requests;
  }
  s2=(sample & (Flush|Kill)) ? std::nullopt : s1;
  s1=(sample & Fire) ? std::optional<Attempt>{{address,request}} : std::nullopt;
  ++cycle;
}
extern "C" void fallback_trace_finish() {
  rheg::graph().validate();
  if(expected || checked!=9 || held!=1 || successors<50) fail("missing successor or fallback coverage");
  std::printf("Exact fetch ancestry passed: %u ordinary S0 -> S0 successors; %u S2 -> S0 fallbacks, including %u blocked/retained redirect\n",successors,checked,held);
}
