// Reconstructs context binding and lineage from public transfers, independently of metadata RTL.
// SPDX-License-Identifier: Apache-2.0
#include "../../../../../rheg/runtime/rheg.h"
#include "event-instance_manifest.h"
#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
rheg::Graph expected;
std::array<unsigned,6> sequences{};
std::array<bool,4> bound{};
std::uint64_t cycle = 0;
unsigned checked = 0, epochs = 0;
bool in_reset = true;
void require(bool condition) { if (!condition) { std::fprintf(stderr,"instance context scoreboard mismatch\n"); std::abort(); } }
}
extern "C" void event_instance_bind() {
  rheg::graph().bind_manifest(rheg_generated::manifest());
  expected.bind_manifest(rheg_generated::manifest());
}
extern "C" void event_instance_sample(unsigned reset, unsigned inputs, unsigned outputs,
    unsigned chip, unsigned a, unsigned b, std::uint64_t bank) {
  expected.reset(reset);
  if (reset) { cycle = 0; sequences = {}; bound = {}; in_reset = true; return; }
  if (in_reset) ++epochs;
  in_reset = false;
  const std::array<std::uint64_t,4> values{chip,a,b,bank};
  for (unsigned scope = 0; scope < 4; ++scope) {
    const bool active = scope ? bool((inputs | outputs) & (1u << (scope-1))) : bool(inputs | outputs);
    if (active && !bound[scope]) { expected.record_instance(scope,values[scope],cycle); bound[scope] = true; }
  }
  for (unsigned lane = 0; lane < 3; ++lane) {
    if (inputs & (1u << lane)) expected.record_node({2*lane,sequences[2*lane]++},cycle,0);
    if (outputs & (1u << lane)) {
      const rheg::Ref child{2*lane+1,sequences[2*lane+1]++};
      expected.record_node(child,cycle,0); expected.record_edge({2*lane,child.sequence},child);
    }
  }
  ++cycle;
}
extern "C" void event_instance_check() {
  const auto actual = rheg::graph().snapshot(), oracle = expected.snapshot();
  require(actual.json() == oracle.json()); ++checked;
}
extern "C" void event_instance_finish() {
  require(checked == 26 && epochs == 2 && expected.nodes.size() == 10 && expected.snapshot().instances().size() == 4);
  std::puts("instance context RTL scoreboard passed");
}
