// Runs all Highway 32/64-bit hash families as dependent-latency and vector-array workloads.
// SPDX-License-Identifier: Apache-2.0
#include "benchmark.h"
#include "hwy/aligned_allocator.h"
#include "hwy/contrib/hash/hash-inl.h"
template <class Hash>
void BenchHash(const Hash& hash) {
  using T = typename Hash::LaneType;
  auto input = hwy::AllocateAligned<T>(256);
  HWY_ASSERT(input);
  HighwaySetPhase(HighwayPhase::Inputs);
  for (size_t i = 0; i < 256; ++i) input[i] = static_cast<T>(Mix(i));
  HighwaySetPhase(HighwayPhase::Setup);
  Measure(hash.Name(), 32, [&] {
    T value = input[0];
    for (size_t i = 0; i < 32; ++i) value = hash(value);
    Consume(value);
  });
  Measure(hash.Name(), 256, [&] { hn::HashArray(hash, input.get(), 256); Consume(input.get()); });
}
void RunBenchmarks() {
  hn::ForeachHash(0x12345678u, [](const auto& hash) { BenchHash(hash); });
  hn::ForeachHash64(0x123456789abcdef0ull, [](const auto& hash) { BenchHash(hash); });
}
