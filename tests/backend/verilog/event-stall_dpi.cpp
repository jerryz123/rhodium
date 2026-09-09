// Scores stall observations against public offers and accepted-token FIFOs, never trace internals.
#include "../../../rheg/runtime/rheg.h"
#include "event-stall_manifest.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>

namespace {
struct Token { rheg::Ref parent; unsigned payload; };
struct Lane {
  std::deque<Token> pending;
  std::array<std::uint64_t, 4> sequence{};
};
std::array<Lane, 3> lanes;
std::array<unsigned, 3> offered_stalls{}, blocked_stalls{}, transfers{}, simultaneous{};
unsigned resets_with_pending = 0, changed_offers = 0, withdrawals = 0, bubbles = 0;
std::array<unsigned, 3> last_payload{};
std::array<bool, 3> was_blocked{};
rheg::Graph expected;
std::uint64_t cycle = 0;
bool in_reset = true;
[[noreturn]] void fail(const std::string& message) {
  std::fprintf(stderr, "%s\n", message.c_str()); std::abort();
}
rheg::Ref node(unsigned lane, unsigned local, unsigned payload) {
  const unsigned site = (local < 2 ? 0 : 6) + lane * 2 + local % 2;
  rheg::Ref ref{site, lanes[lane].sequence[local]++};
  expected.nodes[ref] = {true, cycle, 8, {{0, payload}}};
  return ref;
}
}
extern "C" void event_stall_bind() {
  rheg::graph().bind_manifest(rheg_generated::manifest());
  rheg::graph().bind_timing({100000000, 0});
}
extern "C" void event_stall_sample(unsigned lane, unsigned reset, unsigned valid,
    unsigned ready, unsigned payload, unsigned out_valid, unsigned out_ready, unsigned out_payload) {
  auto& state = lanes.at(lane);
  in_reset = reset;
  if (reset) {
    if (!state.pending.empty()) ++resets_with_pending;
    state = Lane{}; was_blocked[lane] = false;
    expected.clear(); cycle = 0; return;
  }
  if (was_blocked[lane] && valid && payload != last_payload[lane]) ++changed_offers;
  if (was_blocked[lane] && !valid) ++withdrawals;
  if (!valid) ++bubbles;
  was_blocked[lane] = valid && !ready; last_payload[lane] = payload;
  if (valid && ready) state.pending.push_back({node(lane, 0, payload), payload});
  if (valid && !ready) { node(lane, 2, payload); ++offered_stalls[lane]; }
  if (out_valid && !out_ready) {
    const auto stall = node(lane, 3, out_payload);
    ++blocked_stalls[lane];
    // A blocked direct offer has never transferred at the preceding annotation.
    // Buffered offers instead refer to the oldest publicly accepted transaction.
    if (lane != 0) {
      if (state.pending.empty() || state.pending.front().payload != out_payload)
        fail("blocked output has no matching accepted token");
      expected.edges.insert({state.pending.front().parent, stall});
    }
  }
  if (out_valid && out_ready) {
    if (state.pending.empty() || state.pending.front().payload != out_payload)
      fail("output transfer order/payload mismatch");
    expected.edges.insert({state.pending.front().parent, node(lane, 1, out_payload)});
    state.pending.pop_front(); ++transfers[lane];
    if (valid && ready) ++simultaneous[lane];
  }
}
extern "C" void event_stall_check() {
  rheg::graph().validate();
  if (rheg::graph().json() != expected.json())
    fail("stall graph differs at cycle " + std::to_string(cycle) + "\nactual: " + rheg::graph().json() + "\nexpected: " + expected.json());
  if (!in_reset) ++cycle;
}
extern "C" void event_stall_finish() {
  for (unsigned lane = 0; lane < lanes.size(); ++lane)
    if (!lanes[lane].pending.empty() || !offered_stalls[lane] || !blocked_stalls[lane] || !transfers[lane] || !simultaneous[lane])
      fail("stall fixture failed to drain or cover each lane");
  if (!resets_with_pending || !changed_offers || !withdrawals || !bubbles)
    fail("stall fixture missed reset, changed offer, withdrawal, or bubble coverage");
  const auto snapshot = rheg::graph().snapshot();
  if (const auto path = std::getenv("RHEG_STALL_SNAPSHOT")) {
    std::ofstream output(path); output << snapshot.json(); output.close();
    if (!output) fail("cannot write stall snapshot");
  }
  std::printf("stall observations passed: changed=%u withdrawn=%u pending-reset=%u\n", changed_offers, withdrawals, resets_with_pending);
}
