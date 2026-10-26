// Exercises Highway vector subtraction and partial loads/stores in a bounded adjacent-difference workload.
// SPDX-License-Identifier: Apache-2.0
#include "benchmark.h"
#include "hwy/aligned_allocator.h"
template <class T>
void BenchType(const char* name) {
  const hn::ScalableTag<T> d;
  auto input = hwy::AllocateAligned<T>(257), output = hwy::AllocateAligned<T>(257);
  HWY_ASSERT(input && output);
  HighwaySetPhase(HighwayPhase::Inputs);
  for (size_t i = 0; i < 257; ++i) input[i] = static_cast<T>(Mix(i) % 127);
  HighwaySetPhase(HighwayPhase::Setup);
  for (size_t n : kSizes) Measure(name, n, [&] {
    output[0] = input[0];
    for (size_t i = 1; i < n; i += hn::Lanes(d)) {
      const size_t count = HWY_MIN(n - i, hn::Lanes(d));
      const auto current = hn::LoadN(d, input.get() + i, count);
      const auto previous = hn::LoadN(d, input.get() + i - 1, count);
      hn::StoreN(hn::Sub(current, previous), d, output.get() + i, count);
    }
    Consume(output.get());
  });
}
void RunBenchmarks() {
  BenchType<int16_t>("delta.i16");
  BenchType<int32_t>("delta.i32");
  BenchType<float>("delta.f32");
  BenchType<double>("delta.f64");
}
