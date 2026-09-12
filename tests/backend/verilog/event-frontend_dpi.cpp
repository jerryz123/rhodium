// Reconstructs frontend occurrences from public memory and instruction transfers, not trace state.
#include "../../../rheg/runtime/rheg.h"
#include "event-frontend_manifest.h"
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <vector>

namespace {
using namespace test_sites;
struct Attempt { std::uint64_t pc; rheg::Ref ref; };
struct Word { std::uint64_t base; unsigned bits; bool fault; rheg::Ref ref; };
std::optional<Attempt> s1, s2;
std::deque<Word> words;
rheg::Graph expected;
std::map<unsigned, std::uint64_t> sequences;
std::uint64_t cycle = 0, next_attempt = 0, cursor = 0;
unsigned shared = 0, straddles = 0, faults = 0, stalls = 0, canceled = 0;
unsigned live_transfers = 0, live_straddles = 0;
std::set<rheg::Ref> consumed;
[[noreturn]] void fail(const std::string& message) { std::fprintf(stderr, "%s\n", message.c_str()); std::abort(); }
rheg::Ref node(unsigned site, std::uint64_t pc, unsigned flags = 0, unsigned flag_bits = 0,
    const std::vector<rheg::Ref>& parents = {}) {
  rheg::Ref ref{site, sequences[site]++};
  auto& n = expected.nodes[ref];
  n.present = true; n.cycle = cycle; n.width = 64 + flag_bits;
  const auto low = (pc << flag_bits) | flags;
  n.words[0] = std::uint32_t(low); n.words[1] = std::uint32_t(low >> 32);
  if (flag_bits) n.words[2] = std::uint32_t(pc >> (64 - flag_bits));
  for (auto parent : parents) expected.edges.insert({parent, ref});
  return ref;
}
}
extern "C" void event_frontend_sample(unsigned reset, unsigned clear, unsigned recovery, unsigned restart,
    std::uint64_t restart_pc, unsigned request_fire, std::uint64_t request_address,
    unsigned response_valid, unsigned replay, unsigned bits, unsigned page_fault, unsigned access_fault,
    unsigned fetched_valid, unsigned ready, std::uint64_t pc, std::uint64_t sequential_pc,
    std::uint64_t predicted_next_pc) {
  if (reset) {
    expected.clear(); sequences.clear(); words.clear(); s1.reset(); s2.reset();
    cycle = next_attempt = cursor = 0; consumed.clear(); return;
  }
  std::optional<Attempt> incoming, next_s2;
  const bool replaying = s2 && response_valid && replay;
  if (request_fire) {
    const auto candidate = replaying ? s2->pc : next_attempt;
    if ((candidate & ~3ULL) != request_address) fail("request occurrence PC mismatch");
    incoming = Attempt{candidate, node(frontend_s0_request, candidate)};
    next_attempt = (candidate & ~3ULL) + 4;
  } else if (replaying) next_attempt = s2->pc;
  if (s1) {
    auto ref = node(frontend_s1_lookup, s1->pc, 0, 0, {s1->ref});
    if (!clear && !replaying) next_s2 = Attempt{s1->pc, ref};
  }
  if (s2) {
    const bool admitted = !clear && response_valid && !replay;
    const unsigned flags = (replay << 3) | (unsigned(admitted) << 2) |
        (unsigned(admitted && page_fault) << 1) | unsigned(admitted && access_fault);
    auto outcome = node(frontend_s2_outcome, s2->pc, flags, 4, {s2->ref});
    if (admitted) {
      auto base = s2->pc & ~3ULL;
      words.push_back({base, bits, bool(page_fault || access_fault), outcome});
    }
  }
  // The fall-through queue can expose this cycle's admitted S2 packet.
  // The abstract word stream retains a word until all of its parcels are used.
  if (fetched_valid) {
    if (recovery || words.empty() || (pc & ~3ULL) != words.front().base) fail("frontend word order mismatch");
    const auto& head = words.front();
    const bool upper = pc & 2;
    const unsigned half = upper ? head.bits >> 16 : head.bits & 65535;
    const bool compressed = !head.fault && (half & 3) != 3;
    const bool following = !head.fault && upper && !compressed;
    std::vector<rheg::Ref> parents{head.ref};
    if (following) {
      if (words.size() < 2) fail("straddle lacks second accepted word");
      parents.push_back(words[1].ref);
    }
    node(ready ? core_s1_fetch : core_s1_fetch_stall, pc, 0, 0, parents);
    if (!ready) ++stalls;
    else {
      straddles += following; faults += head.fault || (following && words[1].fault);
      bool live = false;
      for (auto parent : parents) live |= expected.nodes.at(parent).cycle == cycle;
      live_transfers += live;
      live_straddles += live && following;
      for (auto parent : parents) if (!consumed.insert(parent).second) ++shared;
      const bool predicted = predicted_next_pc != sequential_pc;
      const unsigned release = compressed && !upper && !predicted ? 0 : predicted && following ? 2 : 1;
      for (unsigned i = 0; i < release; ++i) words.pop_front();
      cursor = predicted_next_pc;
    }
  }
  if (clear) {
    canceled += !words.empty() || bool(s1) || bool(s2);
    if (recovery) words.clear();
    s1.reset(); s2.reset();
    if (restart) { cursor = restart_pc; next_attempt = restart_pc; }
  } else { s1 = incoming; s2 = next_s2; }
  ++cycle;
}
extern "C" void event_frontend_check() {
  if (rheg::graph().json() != expected.json())
    fail("frontend graph mismatch\nactual: " + rheg::graph().json() + "\nexpected: " + expected.json());
}
extern "C" void event_frontend_bind() {
  rheg::graph().bind_manifest(rheg_generated::manifest());
}
extern "C" void event_frontend_finish() {
  rheg::graph().validate();
  if (!shared || !straddles || !faults || !stalls || !canceled || !live_transfers || !live_straddles)
    fail("frontend trace coverage incomplete: shared=" + std::to_string(shared) +
         " straddles=" + std::to_string(straddles) + " faults=" + std::to_string(faults) +
         " stalls=" + std::to_string(stalls) + " canceled=" + std::to_string(canceled) +
         " live=" + std::to_string(live_transfers) + " live-straddles=" + std::to_string(live_straddles));
  std::printf("Frontend exact ancestry passed: %u shared parents, %u straddles, %u faults, %u stalls, %u cancellations, %u live transfers (%u straddles)\n",
      shared, straddles, faults, stalls, canceled, live_transfers, live_straddles);
}
