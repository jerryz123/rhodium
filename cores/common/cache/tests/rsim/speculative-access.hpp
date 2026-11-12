// Exercises the same speculative doubleword offers at each core's EX/MEM boundary.
// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace speculative_access {
constexpr unsigned load = 1, store = 2, normal = 0, hlvx = 2;
constexpr unsigned slow = 0, load_hit = 1, store_hit = 2, replay = 3, access_fault = 5;
inline bool issues_lookup(unsigned outcome) { return outcome == load_hit || outcome == store_hit; }
}

template <typename Probe> void check_speculative_data_permissions(Probe probe) {
  using namespace speculative_access;
  probe(0x2100, load, normal, slow);         // Read-only device, nonallocating.
  probe(0x2100, store, normal, access_fault);
  probe(0x2500, load, normal, load_hit);     // Cacheable, readable, non-executable RAM.
  probe(0x2500, store, normal, store_hit);
  probe(0x2500, load, hlvx, access_fault);
  probe(0x2700, load, normal, access_fault); // First byte mapped, last byte unmapped.
  probe(0x2700, store, normal, access_fault);
}

template <typename Busy, typename Probe> void check_ordered_io_replay(Busy &busy, Probe probe) {
  using namespace speculative_access;
  // Reissue the same load after the older ordered operation clears.
  const auto retry = [&](unsigned address, unsigned expected) {
    busy = 1;
    probe(address, load, normal, replay);
    busy = 0;
    probe(address, load, normal, expected);
  };
  retry(0x2500, load_hit);
  retry(0x2700, access_fault);
  retry(0x2501, slow);
}

// Keep the same aligned load in EX for the retry. Invalidation arrives after
// its first lookup reaches MEM and crosses the edge before the retry resolves.
template <class Flag, class Tick, class Expect>
void check_warm_lookup_invalidation(Flag &invalidate, Tick tick, Expect expect) {
  tick();
  expect(speculative_access::load_hit);
  invalidate = 1;
  expect(speculative_access::replay);
  tick();
  invalidate = 0;
  expect(speculative_access::slow);
}
