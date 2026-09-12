// Scores host command ownership across fragments, write data, errors, completion stalls, and reset.
#include "../../../rheg/runtime/rheg.h"
#include "event-fesvr_manifest.h"
#include <cstdio>
#include <cstdlib>

namespace {
rheg::Graph expected;
std::optional<rheg::Ref> owner;
std::map<unsigned, std::uint64_t> sequences;
std::uint64_t cycle = 0;
unsigned fragments = 0, fragmented = 0, writes = 0, errors = 0, completed = 0, stalled = 0, resets = 0;
[[noreturn]] void fail(const char* message) { std::fprintf(stderr, "%s at cycle %llu\n", message, (unsigned long long)cycle); std::abort(); }
rheg::Ref node(unsigned site, bool child) {
  rheg::Ref ref{site, sequences[site]++};
  expected.nodes[ref] = {true, cycle, 0, {}};
  if (child) {
    if (!owner) fail("host output without an accepted command");
    expected.edges.insert({*owner, ref});
  }
  return ref;
}
}
extern "C" void event_fesvr_bind() { rheg::graph().bind_manifest(rheg_generated::manifest()); }
extern "C" void event_fesvr_sample(unsigned reset, unsigned command, unsigned request,
    unsigned write_data, unsigned completion, unsigned status, unsigned blocked) {
  if (reset) {
    resets += owner.has_value(); owner.reset(); sequences.clear(); expected.clear(); cycle = 0; fragments = 0;
    return;
  }
  if (command) {
    if (owner) fail("overlapping host command owners");
    owner = node(test_sites::command, false); fragments = 0;
  }
  if (request) { node(test_sites::request, true); ++fragments; }
  if (write_data) { node(test_sites::write_data, true); ++writes; }
  if (completion) {
    node(test_sites::completion, true); ++completed;
    fragmented += fragments >= 4; errors += status != 0; owner.reset();
  }
  stalled += blocked;
  ++cycle;
}
extern "C" void event_fesvr_check() {
  if (rheg::graph().json() != expected.json()) fail("host exact occurrence graph mismatch");
}
extern "C" void event_fesvr_finish() {
  rheg::graph().validate();
  if (owner || fragmented < 2 || !writes || !errors || !completed || !stalled || resets < 3)
    fail("host trace coverage incomplete");
  std::printf("Host ownership passed: %u commands, %u fragmented, %u write beats, %u errors, %u stalled cycles, %u pending resets\n",
      completed, fragmented, writes, errors, stalled, resets);
}
