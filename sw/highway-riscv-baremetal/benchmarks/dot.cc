// Runs Highway's floating-point dot product with bounded aligned and tail-sized inputs.
// SPDX-License-Identifier: Apache-2.0
#include "benchmark.h"
#include "hwy/aligned_allocator.h"
#include "hwy/contrib/dot/dot-inl.h"
template <class T>
void BenchType(const char* name) {
  const hn::ScalableTag<T> d;
  auto a = hwy::AllocateAligned<T>(257), b = hwy::AllocateAligned<T>(257);
  HWY_ASSERT(a && b);
  HighwaySetPhase(HighwayPhase::Inputs);
  for (size_t i = 0; i < 257; ++i) {
    a[i] = hwy::ConvertScalarTo<T>(static_cast<float>(i % 17) * 0.125f);
    b[i] = hwy::ConvertScalarTo<T>(static_cast<float>(i % 7) * 0.25f);
  }
  HighwaySetPhase(HighwayPhase::Setup);
  for (size_t n : kSizes) Measure(name, n, [&] {
    const auto sum = hn::Dot::Compute<0>(d, a.get(), b.get(), n);
    Consume(sum);
  });
}
void RunBenchmarks() {
  BenchType<hwy::float16_t>("dot.f16");
  BenchType<float>("dot.f32");
  BenchType<double>("dot.f64");
}
