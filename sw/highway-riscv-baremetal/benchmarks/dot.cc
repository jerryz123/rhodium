// Runs Highway dot products from constant aligned inputs without target-side generation.
// SPDX-License-Identifier: Apache-2.0
#include "benchmark.h"
#include "hwy/aligned_allocator.h"
#include "hwy/contrib/dot/dot-inl.h"
#include "constant-inputs.h"
template <class T>
void BenchType(const char* name) {
  const hn::ScalableTag<T> d;
  alignas(64) static constexpr auto left = DotInputs<T, 17, 8>();
  alignas(64) static constexpr auto right = DotInputs<T, 7, 4>();
  const T* a = OpaqueInput(left);
  const T* b = OpaqueInput(right);
  for (size_t n : kSizes) Measure(name, n, [&] {
    const auto sum = hn::Dot::Compute<0>(d, a, b, n);
    Consume(sum);
  });
}
void RunBenchmarks() {
  BenchType<hwy::float16_t>("dot.f16");
  BenchType<float>("dot.f32");
  BenchType<double>("dot.f64");
}
