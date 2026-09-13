// Reconstructs exact retained/live parents from public controls and transfers.
// SPDX-License-Identifier: Apache-2.0
#include "../../../rheg/runtime/rheg.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <set>
#include <vector>

namespace {
struct Item { unsigned bits; std::vector<rheg::Ref> parents; };
std::deque<Item> words;
Item held{};
bool holding = false;
rheg::Graph expected;
std::array<std::uint64_t, 2> sequences{};
std::uint64_t cycle = 0;
unsigned pairs = 0, repeated = 0, canceled = 0, replacements = 0, stalls = 0;
unsigned triples = 0, live_only = 0, live_without_capture = 0;
std::array<unsigned, 4> releases{};
std::set<rheg::Ref> used;
[[noreturn]] void fail(const char* message) { std::fprintf(stderr, "%s\n", message); std::abort(); }
rheg::Ref node(unsigned site, unsigned bits, const std::vector<rheg::Ref>& parents = {}) {
  rheg::Ref ref{site, sequences[site]++};
  auto& n = expected.nodes[ref];
  n.present = true; n.cycle = cycle; n.width = 8; n.words[0] = bits;
  for (const auto& parent : parents) expected.edges.insert({parent, ref});
  return ref;
}
}
extern "C" void event_window_sample(unsigned reset, unsigned flush, unsigned emit,
    unsigned release, unsigned selected, unsigned live, unsigned capture, unsigned valid, unsigned payload, unsigned ready,
    unsigned out_valid, unsigned out_payload, unsigned count) {
  if (reset) {
    expected.clear(); words.clear(); sequences.fill(0); holding = false; cycle = 0; used.clear(); return;
  }
  if (count != words.size() || bool(out_valid) != holding || (holding && out_payload != held.bits))
    fail("window functional mismatch");
  if (holding && !ready) ++stalls;
  if (holding && ready) {
    node(1, held.bits, held.parents);
    if (held.parents.size() == 2) ++pairs;
    if (held.parents.size() == 3) ++triples;
    for (auto parent : held.parents) if (!used.insert(parent).second) ++repeated;
  }
  rheg::Ref incoming{};
  if (valid) incoming = node(0, payload);
  if (!holding || ready) {
    holding = emit && !flush;
    if (holding) {
      held = {};
      for (unsigned i = 0; i < 2; ++i) if (selected & (1u << i)) {
        if (i >= words.size()) fail("invalid test window selection");
        held.bits ^= words[i].bits;
        held.parents.insert(held.parents.end(), words[i].parents.begin(), words[i].parents.end());
      }
      if (live) {
        if (!valid) fail("invalid live selection");
        held.bits ^= payload;
        held.parents.push_back(incoming);
        live_only += !selected;
        live_without_capture += !capture;
      }
    }
  }
  const bool full = words.size() == 3;
  if (flush) { canceled += !words.empty(); words.clear(); }
  else {
    if (release > words.size()) fail("invalid test release");
    ++releases[release];
    while (release--) words.pop_front();
    if (valid && capture) { replacements += full; words.push_back({payload, {incoming}}); }
  }
  ++cycle;
}
extern "C" void event_window_check() {
  if (rheg::graph().json() != expected.json()) fail("window graph differs from public-transfer model");
}
extern "C" void event_window_finish() {
  if (!pairs || !triples || !live_only || !live_without_capture || !repeated || !canceled || !replacements || !stalls || !releases[0] || !releases[1] || !releases[2] || !releases[3])
    fail("window test missed required coverage");
}
