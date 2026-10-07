// Checks retired next-PC/RAS captures against the fetching core's independent execution oracle.
// SPDX-License-Identifier: Apache-2.0
#include "../../../../../rheg/runtime/rheg.h"
#include "rv2wide-fetch_manifest.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <deque>

namespace {
struct Expected { std::uint64_t pc; unsigned instruction, prediction, ras_mismatch; };
std::array<std::deque<Expected>,2> pending;
std::array<std::uint64_t,2> sequence{};
constexpr unsigned sites[] = {rv2wide_fetch_sites::wb0,rv2wide_fetch_sites::wb1};
std::array<unsigned,3> predictions{};
unsigned checked = 0;
[[noreturn]] void fail(const char* reason) {
  std::fprintf(stderr,"RV2Wide retirement trace: %s\n",reason);
  std::abort();
}
}

// Installs the exact fixture schema before the first reset and transfer callbacks.
extern "C" void rv2wide_fetch_trace_bind() {
  rheg::graph().bind_manifest(rheg_generated::manifest());
}

extern "C" void rv2wide_fetch_trace_expect(unsigned lane, std::uint64_t pc, unsigned instruction, unsigned prediction, unsigned ras_mismatch) {
  if (lane>=2 || prediction>=3 || ras_mismatch>=2) fail("invalid oracle expectation");
  pending[lane].push_back({pc,instruction,prediction,ras_mismatch});
}

extern "C" void rv2wide_fetch_trace_check(unsigned reset) {
  if (reset) {
    for (auto& queue:pending) if (!queue.empty()) fail("missing retirement before reset");
    sequence.fill(0);
    return;
  }
  const auto& graph=rheg::graph();
  for (unsigned lane=0;lane<2;++lane) {
    rheg::Ref ref{sites[lane],sequence[lane]};
    if (graph.nodes.count(ref)) {
      if (pending[lane].empty()) fail("unexpected retirement (squash, replay, trap, or duplicate)");
      const auto wanted=pending[lane].front(); pending[lane].pop_front();
      if (graph.field(ref,"pc").unsigned_value()!=wanted.pc ||
          graph.field(ref,"instruction").unsigned_value()!=wanted.instruction ||
          graph.field(ref,"branch_prediction").unsigned_value()!=wanted.prediction ||
          graph.field(ref,"ras_mismatch").unsigned_value()!=wanted.ras_mismatch)
        fail("retired PC, raw instruction, next-PC accuracy, or RAS action differs from oracle");
      ++sequence[lane]; ++checked; ++predictions[wanted.prediction];
    }
    if (!pending[lane].empty()) fail("public retirement has no matching trace occurrence");
  }
}

// Checks completed occurrence identities, payloads, and lineage against that schema.
extern "C" void rv2wide_fetch_trace_finish() {
  (void)rheg::graph().snapshot();
  for (auto count:predictions) if (!count) fail("missing nonbranch/correct/mispredicted coverage");
  std::printf("RV2Wide retirement prediction trace passed: %u retired, %u correct, %u mispredicted, %u nonbranches\n",
              checked,predictions[1],predictions[2],predictions[0]);
}
