// Checks exact retired instruction order, prediction captures, and retained MEM lineage from public stimuli.
// SPDX-License-Identifier: Apache-2.0
#include "../../../../../rheg/runtime/rheg.h"
#include "rv5stage-retirement-trace_manifest.h"
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>

namespace {
struct Expected { std::uint64_t pc; unsigned instruction, prediction; };
std::deque<Expected> pending;
std::uint64_t sequence = 0;
unsigned correct = 0, missed = 0, ordinary = 0;
[[noreturn]] void fail(const char* reason) {
  std::fprintf(stderr,"retirement trace: %s at WB sequence %llu\n",reason,(unsigned long long)sequence);
  std::abort();
}
}
extern "C" void retirement_trace_init() {
  rheg::graph().bind_manifest(rheg_generated::manifest());
  rheg::graph().bind_timing({100000000});
}
extern "C" void retirement_trace_expect(std::uint64_t pc, unsigned instruction, unsigned prediction) {
  pending.push_back({pc,instruction,prediction});
}
extern "C" void retirement_trace_check(unsigned reset) {
  if (reset) { sequence=0; pending.clear(); correct=missed=ordinary=0; return; }
  const auto& graph = rheg::graph(); graph.validate();
  const rheg::Ref ref{retirement_sites::wb,sequence};
  if (!graph.nodes.count(ref)) return;
  if (graph.nodes.at(ref).ancestry_unknown) fail("retirement ancestry is incomplete");
  if (pending.empty()) fail("unexpected retirement (squash, replay, trap, or duplicate)");
  const auto wanted=pending.front(); pending.pop_front();
  if (graph.field(ref,"pc").unsigned_value()!=wanted.pc ||
      graph.field(ref,"instruction").unsigned_value()!=wanted.instruction ||
      graph.field(ref,"branch_prediction").unsigned_value()!=wanted.prediction) {
    std::fprintf(stderr,"expected pc=%llx instruction=%08x prediction=%u; got pc=%llx instruction=%08llx prediction=%llu\n",
      (unsigned long long)wanted.pc,wanted.instruction,wanted.prediction,
      (unsigned long long)graph.field(ref,"pc").unsigned_value(),
      (unsigned long long)graph.field(ref,"instruction").unsigned_value(),
      (unsigned long long)graph.field(ref,"branch_prediction").unsigned_value());
    fail("wrong instruction or prediction result");
  }
  unsigned parents=0;
  rheg::Ref mem{};
  for (const auto& edge:graph.edges) if (edge.second.site==ref.site && edge.second.sequence==ref.sequence && edge.first.site==retirement_sites::mem) {
    ++parents;
    mem=edge.first;
    if (graph.nodes.at(edge.first).cycle>=graph.nodes.at(ref).cycle ||
        graph.field(edge.first,"pc").unsigned_value()!=wanted.pc ||
        graph.field(edge.first,"instruction").unsigned_value()!=wanted.instruction) fail("lost retained MEM occurrence");
  }
  if (parents!=1) fail("retirement must retain exactly one MEM occurrence");
  for (const auto& edge:graph.edges) if (edge.second.site==ref.site && edge.second.sequence==ref.sequence && edge.first.site!=retirement_sites::mem) {
    if (edge.first.site!=retirement_sites::ex || !graph.edges.count({edge.first,mem}))
      fail("local result belongs to another MEM instruction");
  }
  if (wanted.prediction==1) ++correct; else if (wanted.prediction==2) ++missed; else ++ordinary;
  ++sequence;
}
extern "C" void retirement_trace_pending(unsigned count) {
  if (pending.size()!=count) fail("retirement occurred at the wrong time");
}
extern "C" void retirement_trace_finish() {
  if (!pending.empty() || correct<4 || missed<4 || ordinary<8) fail("missing prediction/retirement coverage");
  if (const auto* path=std::getenv("RHEG_RETIREMENT_SNAPSHOT")) {
    std::ofstream file(path); file << rheg::graph().snapshot().json();
    if (!file) fail("cannot write snapshot");
  }
  std::printf("retirement trace passed: %llu retired, %u correct, %u mispredicted, %u nonbranches\n",(unsigned long long)sequence,correct,missed,ordinary);
}
