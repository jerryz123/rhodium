// Exercises adjacent differences from constant inputs with writable aligned output storage.
// SPDX-License-Identifier: Apache-2.0
#include "benchmark.h"
#include "hwy/aligned_allocator.h"
#include "constant-inputs.h"
template <class T>
void BenchType(const char* name) {
  const hn::ScalableTag<T> d;
  alignas(64) static constexpr auto values = DeltaInputs<T>();
  const T* input = OpaqueInput(values);
  auto output = hwy::AllocateAligned<T>(257);
  HWY_ASSERT(output);
  for (size_t n : kSizes) Measure(name, n, [&] {
    output[0] = input[0];
    for (size_t i = 1; i < n; i += hn::Lanes(d)) {
      const size_t count = HWY_MIN(n - i, hn::Lanes(d));
      const auto current = hn::LoadN(d, input + i, count);
      const auto previous = hn::LoadN(d, input + i - 1, count);
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
