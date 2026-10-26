// Prepares valid sparse Highway cuckoo query inputs without running large scalar placement searches.
// SPDX-License-Identifier: Apache-2.0
#ifndef RHODIUM_HIGHWAY_HASH_INPUTS_H_
#define RHODIUM_HIGHWAY_HASH_INPUTS_H_
#include <utility>
#include "hwy/aligned_allocator.h"
#include "hwy/contrib/hash/cuckoo2x2-inl.h"
#include "hwy/contrib/hash/cuckoo-inl.h"
namespace highway_inputs {
namespace hn = hwy::HWY_NAMESPACE;

// Fill the documented public fingerprint layout, alternating primary/secondary
// placement so the upstream query sees both tags. The full 18-bit bucket space
// preserves Highway's zero-false-positive contract.
inline hn::Cuckoo2x2 Cuckoo2x2(const hwy::AlignedVector<uint32_t>& keys) {
  hwy::Cuckoo2x2Data data;
  data.config = hwy::Cuckoo2x2Config(size_t{1} << 18, 0x12345678u);
  data.entries.resize(data.NumBuckets(), 0);
  const hn::WeakTwoMul hash(data.config.hash_key);
  for (size_t i = 0; i < keys.size(); ++i) {
    const uint32_t value = hash(keys[i]);
    const bool secondary = (i & 1) != 0;
    const uint32_t bucket = (value & data.config.bucket_mask) ^ (secondary ? (value >> 18) + 1 : 0);
    const uint32_t fingerprint = (value >> 18) | (secondary ? 0x8000u : 0x4000u);
    auto& entry = data.entries[bucket];
    if ((entry & 0xffffu) == 0) entry |= fingerprint;
    else { HWY_ASSERT((entry >> 16) == 0); entry |= fingerprint << 16; }
    if (secondary) ++data.num_secondary;
    else ++data.num_primary;
  }
  return hn::Cuckoo2x2(std::move(data));
}

// The public raw-slot constructor lets a benchmark supply its own input table.
// Sparse deterministic keys need no matching/optimization graph. Both bucket
// choices are populated; the upstream conversion still builds packed u16 slots.
template <class Traits>
hn::CuckooTableT<Traits> Cuckoo(const hwy::AlignedVector<uint32_t>& keys) {
  using Table = hn::CuckooTableT<Traits>;
  const hn::CuckooConfigT<Traits> config(keys.size());
  const typename Traits::HashT primary(0x12345678u), secondary(0x87654321u);
  hwy::AlignedVector<uint32_t> slots(config.NumSlots(), Table::kEmpty);
  uint32_t num_primary = 0;
  for (size_t i = 0; i < keys.size(); ++i) {
    const bool use_secondary = (i & 1) != 0;
    const auto hash = use_secondary ? secondary(keys[i]) : primary(keys[i]);
    const size_t slot = config.BucketFromHash(hash) * Traits::kBucketSize;
    HWY_ASSERT(slots[slot] == Table::kEmpty);
    slots[slot] = keys[i];
    num_primary += !use_secondary;
  }
  return Table(config, primary, secondary, std::move(slots), num_primary);
}
}  // namespace highway_inputs
#endif
