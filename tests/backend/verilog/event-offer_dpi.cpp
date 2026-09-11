// Scores each offer's current parent and qualified transfer/stall against public inputs.
#include "../../../rheg/runtime/rheg.h"
#include "event-offer_manifest.h"
#include <array>
#include <cstdio>
#include <cstdlib>

namespace {
rheg::Graph expected;
std::array<std::uint64_t, 3> sequence{};
std::uint64_t cycle = 0;
bool in_reset = true, was_rejected = false;
unsigned last_payload = 0;
unsigned accepted = 0, stalled = 0, suppressed_transfers = 0, suppressed_stalls = 0;
unsigned replayed = 0, changed = 0, withdrawn = 0, consecutive = 0;
bool was_accepted = false;
[[noreturn]] void fail(const std::string& message) {
  std::fprintf(stderr, "%s\n", message.c_str()); std::abort();
}
rheg::Ref node(unsigned site, unsigned payload) {
  const rheg::Ref ref{site, sequence.at(site)++};
  expected.nodes[ref] = {true, cycle, 8, {{0, payload}}};
  return ref;
}
}
extern "C" void event_offer_bind() {
  rheg::graph().bind_manifest(rheg_generated::manifest());
}
extern "C" void event_offer_sample(unsigned reset, unsigned valid, unsigned ready,
    unsigned qualify, unsigned payload) {
  in_reset = reset;
  if (reset) {
    expected.clear(); sequence = {}; cycle = 0;
    was_rejected = false; was_accepted = false;
    return;
  }
  if (was_rejected && valid && ready && qualify && payload == last_payload) ++replayed;
  if (was_rejected && valid && payload != last_payload) ++changed;
  if (was_rejected && !valid) ++withdrawn;
  if (was_accepted && valid && ready && qualify) ++consecutive;
  if (valid) {
    const auto parent = node(0, payload);
    if (qualify) {
      const auto child = node(ready ? 1 : 2, payload);
      expected.edges.insert({parent, child});
      if (ready) ++accepted; else ++stalled;
    } else {
      if (ready) ++suppressed_transfers; else ++suppressed_stalls;
    }
  }
  was_rejected = valid && !ready;
  was_accepted = valid && ready && qualify;
  last_payload = payload;
}
extern "C" void event_offer_check() {
  rheg::graph().validate();
  if (rheg::graph().json() != expected.json())
    fail("offer graph differs at cycle " + std::to_string(cycle) + "\nactual: " +
      rheg::graph().json() + "\nexpected: " + expected.json());
  if (!in_reset) ++cycle;
}
extern "C" void event_offer_finish() {
  if (!accepted || !stalled || !suppressed_transfers || !suppressed_stalls ||
      !replayed || !changed || !withdrawn || !consecutive)
    fail("offer fixture missed acceptance, qualification, replay, change, withdrawal, or throughput coverage");
  std::printf("offer lineage passed: accepted=%u stalled=%u suppressed-transfers=%u suppressed-stalls=%u replayed=%u changed=%u withdrawn=%u consecutive=%u\n",
    accepted, stalled, suppressed_transfers, suppressed_stalls, replayed, changed, withdrawn, consecutive);
}
