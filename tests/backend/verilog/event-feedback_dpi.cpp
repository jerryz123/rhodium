// Checks feedback lineage against a public-control FIFO model, never payload matching.
#include "../../../rheg/runtime/rheg.h"
#include "event-feedback_manifest.h"
#include <cstdio>
#include <cstdlib>
#include <deque>
namespace {
struct Pending { rheg::Ref ref; unsigned payload, laps; };
std::deque<Pending> queue;
rheg::Graph expected;
std::uint64_t cycle=0, sequences[2]{};
unsigned recirculations=0, repeated_laps=0, stalls=0, concurrent=0, full_stalls=0, pending_resets=0, completions=0;
bool resetting=true;
[[noreturn]] void fail(const char* message) {
  std::fprintf(stderr,"feedback trace: %s at cycle %llu\n",message,(unsigned long long)cycle);
  std::abort();
}
Pending node(unsigned site, unsigned payload) {
  rheg::Ref ref{feedback_sites[site],sequences[site]++};
  expected.nodes[ref]={true,cycle,8,{{0,payload}}};
  return {ref,payload,0};
}
}
extern "C" void feedback_bind() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
extern "C" void feedback_sample(unsigned reset, unsigned select_feedback, unsigned route_feedback,
    unsigned valid, unsigned ready, unsigned payload, unsigned out_valid, unsigned out_ready, unsigned out_payload) {
  resetting=reset;
  if(reset) {
    if(!queue.empty()) ++pending_resets;
    queue.clear(); expected.clear(); cycle=0; sequences[0]=sequences[1]=0;
    return;
  }
  const bool space=queue.size()<3;
  const bool offered=!queue.empty() && !route_feedback;
  if(bool(ready)!=(space && !select_feedback)) fail("input readiness differs from model");
  if(bool(out_valid)!=offered) fail("output validity differs from model");
  if(offered && out_payload!=queue.front().payload) fail("output payload differs from model");
  const bool accept=valid && ready, finish=offered && out_ready;
  const bool feedback=!queue.empty() && route_feedback && select_feedback && space;
  if(offered && !out_ready) ++stalls;
  if(!space && route_feedback && select_feedback) ++full_stalls;
  if(accept && finish) ++concurrent;
  if(feedback) {
    auto owner=queue.front(); queue.pop_front(); ++owner.laps;
    queue.push_back(owner); ++recirculations;
    if(owner.laps>=2) ++repeated_laps;
  }
  if(finish) {
    const auto parent=queue.front(); queue.pop_front();
    const auto child=node(1,out_payload);
    expected.edges.insert({parent.ref,child.ref}); ++completions;
  }
  if(accept) queue.push_back(node(0,payload));
}
extern "C" void feedback_check() {
  rheg::graph().validate();
  if(rheg::graph().json()!=expected.json()) fail("graph differs from exact occurrence scoreboard");
  if(!resetting) ++cycle;
}
extern "C" void feedback_finish() {
  if(!queue.empty()) fail("feedback queue did not drain");
  if(!recirculations || !repeated_laps || !stalls || !concurrent || !full_stalls || !pending_resets || completions<10)
    fail("missing feedback, stall, simultaneous transfer, full, reset, or completion coverage");
  std::printf("Feedback exact ancestry passed: %u laps, %u repeated laps, %u stalls, %u simultaneous transfers, %u full stalls, %u pending resets, %u completions\n",
    recirculations,repeated_laps,stalls,concurrent,full_stalls,pending_resets,completions);
}
