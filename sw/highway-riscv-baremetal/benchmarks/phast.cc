// Exercises Highway PHAST, ShardMul, and both cuckoo hash table query representations with one synchronous worker.
// SPDX-License-Identifier: Apache-2.0
#include "benchmark.h"
#include "hash-inputs.h"
#include "hwy/aligned_allocator.h"
#include "hwy/contrib/hash/phast-inl.h"
#include "hwy/contrib/hash/shardmul-inl.h"
#include "hwy/contrib/hash/cuckoo2x2-inl.h"
#include "hwy/contrib/hash/cuckoo-inl.h"
template <bool Short, bool Pow2> void BenchCuckoo(const hwy::AlignedVector<uint32_t>& keys) {
  // Packed fingerprints require >= 2^18 buckets. One slot per bucket keeps
  // that native invariant without allocating millions of mostly empty slots.
  constexpr size_t buckets = Short ? size_t{1} << 18 : 1;
  constexpr size_t slots_per_bucket = Short ? 1 : 0;
  const hn::CuckooTraits<hn::WeakTwoMul, slots_per_bucket, buckets, Pow2, 0> traits;
  HighwaySetPhase(Short ? HighwayPhase::PackedInit : HighwayPhase::CuckooBuild);
  auto table = [&] {
    if constexpr (Short) return highway_inputs::Cuckoo<decltype(traits)>(keys);
    else return hn::CuckooBuild(traits, hwy::Span(keys));
  }();
  HWY_ASSERT(!table.IsEmpty());
  if constexpr (Short) {
    HighwaySetPhase(HighwayPhase::PackedConvert);
    table.BuildU16Slots();
  }
  HighwaySetPhase(HighwayPhase::Setup);
  const hn::ScalableTag<uint32_t> d;
  Measure(Short ? "cuckoo.u16" : "cuckoo.u32", keys.size(), [&] {
    for (size_t i = 0; i < keys.size(); i += hn::Lanes(d)) {
      const auto absent = [&] {
        if constexpr (Short) return table.template QueryBatchU16<true>(d, keys.data() + i);
        else return table.QueryBatch(d, keys.data() + i);
      }();
      const bool result = hn::AllFalse(d, absent);
      Consume(result);
    }
  });
}
void RunBenchmarks() {
  hwy::ThreadPool pool(0);
  const hn::ScalableTag<uint32_t> d;
  const hn::ScalableTag<uint64_t> d64;
  const size_t count = hwy::RoundUpTo(size_t{64}, 2 * hn::Lanes(d));
  hwy::AlignedVector<uint32_t> keys(count), output(count);
  hwy::AlignedVector<uint64_t> keys64(count);
  HighwaySetPhase(HighwayPhase::Inputs);
  for (size_t i = 0; i < keys.size(); ++i) {
    keys[i] = Mix(i);
    keys64[i] = (uint64_t{Mix(i)} << 32) | Mix(i + 257);
  }
  HighwaySetPhase(HighwayPhase::PhastBuild);
  const auto phast = hn::MakePhast(hwy::Span(keys), 0, pool);
  HighwaySetPhase(HighwayPhase::ShardmulBuild);
  const auto shard = hn::MakeShardMul(hwy::Span(keys64), pool);
  HighwaySetPhase(HighwayPhase::Cuckoo2x2Init);
  const auto cuckoo = highway_inputs::Cuckoo2x2(keys);
  HighwaySetPhase(HighwayPhase::Setup);
  HWY_ASSERT(!phast.Data().IsEmpty() && !shard.IsEmpty() && cuckoo.Data().NumBuckets() > 0);
  Measure("phast.latency", 32, [&] {
    uint32_t value = keys[0];
    for (size_t i = 0; i < 32; ++i) value = phast(value);
    Consume(value);
  });
  Measure("phast.vector", keys.size(), [&] {
    for (size_t i = 0; i < keys.size(); i += 2 * hn::Lanes(d)) {
      auto out0 = hn::Zero(d), out1 = hn::Zero(d);
      phast(d, hn::LoadU(d, keys.data() + i), hn::LoadU(d, keys.data() + i + hn::Lanes(d)), out0, out1);
      hn::StoreU(out0, d, output.data() + i);
      hn::StoreU(out1, d, output.data() + i + hn::Lanes(d));
    }
    Consume(output.data());
  });
  Measure("shardmul.vector", keys64.size(), [&] {
    for (size_t i = 0; i < keys64.size(); i += 2 * hn::Lanes(d64)) {
      const auto out = shard.TwoVec(d, hn::LoadU(d64, keys64.data() + i),
                                   hn::LoadU(d64, keys64.data() + i + hn::Lanes(d64)));
      hn::StoreU(out, d, output.data() + i);
    }
    Consume(output.data());
  });
  Measure("cuckoo2x2.vector", keys.size(), [&] {
    for (size_t i = 0; i < keys.size(); i += hn::Lanes(d)) {
      const bool result = hn::AllFalse(d, cuckoo(d, hn::LoadU(d, keys.data() + i)));
      Consume(result);
    }
  });
  BenchCuckoo<false, false>(keys); BenchCuckoo<false, true>(keys);
  BenchCuckoo<true, false>(keys); BenchCuckoo<true, true>(keys);
}
