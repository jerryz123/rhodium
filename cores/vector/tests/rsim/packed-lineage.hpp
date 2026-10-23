// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "test-sites.h"
#include "../../../../rheg/runtime/rheg.h"
#include <map>
#include <optional>
#include <set>

namespace packed_lineage {
inline std::map<unsigned, unsigned> layout;
inline std::map<unsigned, rheg::Ref> owners;
inline std::map<rheg::Ref, unsigned> parent_uses;
inline std::uint64_t cycle = 0, attempts = 0, writes = 0;
inline unsigned merged = 0, reused = 0;
inline bool resetting = false;
struct Attempt { rheg::Ref ref; unsigned slot, address; };
struct Write { rheg::Ref ref; unsigned row; std::uint64_t mask; std::set<rheg::Ref> parents; };
inline std::optional<Attempt> attempt;
inline std::optional<Write> write;

inline bool equal(rheg::Ref a, rheg::Ref b) {
  return a.site == b.site && a.sequence == b.sequence;
}

inline void begin_case() {
  CHECK(owners.empty());
  layout.clear();
}

inline void sample(bool reset, unsigned bytes, bool offered, unsigned slot,
                   unsigned address, unsigned byte_mask, bool retry, bool store,
                   bool written, unsigned row, std::uint64_t mask) {
  attempt.reset(); write.reset(); resetting = reset;
  if (reset) {
    cycle = attempts = writes = 0;
    owners.clear(); layout.clear(); parent_uses.clear();
    return;
  }
  if (offered) {
    attempt = Attempt{{packed_sites::attempt, attempts++}, slot, address};
    if (!retry && !store) for (unsigned lane = 0; lane < bytes; ++lane) {
      if (!(byte_mask & (1u << lane))) continue;
      auto position = layout.find(address + lane);
      CHECK(position != layout.end());
      CHECK(owners.emplace(position->second, attempt->ref).second);
    }
  }
  if (written) {
    write = Write{{packed_sites::complete, writes++}, row, mask, {}};
    for (unsigned lane = 0; lane < bytes; ++lane) {
      if (!((mask >> (8 * lane)) & 255)) continue;
      auto owner = owners.find(row * bytes + lane);
      CHECK(owner != owners.end());
      write->parents.insert(owner->second);
      owners.erase(owner);
    }
  }
}

inline void check() {
  if (resetting) return;
  const auto &graph = rheg::graph();
  if (attempt) {
    CHECK(graph.nodes.at(attempt->ref).cycle == cycle);
    CHECK(graph.field(attempt->ref, "slot").unsigned_value() == attempt->slot);
    CHECK(graph.field(attempt->ref, "address").unsigned_value() == attempt->address);
  }
  if (write) {
    const auto &node = graph.nodes.at(write->ref);
    CHECK(node.cycle == cycle && !node.ancestry_unknown);
    CHECK(graph.field(write->ref, "row").unsigned_value() == write->row);
    CHECK(graph.field(write->ref, "write_mask").unsigned_value() == write->mask);
    std::set<rheg::Ref> parents;
    for (const auto &edge : graph.edges)
      if (equal(edge.second, write->ref)) parents.insert(edge.first);
    const bool matches = parents.size() == write->parents.size() &&
        std::equal(parents.begin(), parents.end(), write->parents.begin(), equal);
    if (!matches) {
      std::cerr << "packed lineage cycle " << cycle << " row " << write->row
                << " parents " << parents.size() << " expected " << write->parents.size() << '\n';
    }
    CHECK(matches);
    if (parents.size() > 1) ++merged;
    for (auto parent : parents) if (++parent_uses[parent] > 1) ++reused;
  }
  ++cycle;
}

inline void finish() {
  CHECK(owners.empty() && merged && reused);
  rheg::graph().validate();
}
}
