// Exercises the same aligned doubleword offers at each core's EX/MEM boundary.
// SPDX-License-Identifier: Apache-2.0
#pragma once

namespace speculative_access {
constexpr unsigned load = 1, store = 2, normal = 0, hlvx = 2;
constexpr unsigned slow = 0, load_hit = 1, store_hit = 2, access_fault = 5;
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
