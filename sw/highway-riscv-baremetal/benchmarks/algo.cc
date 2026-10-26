// Exercises Highway unique/all-unique and conditional-copy kernels across integer widths and tail lengths.
// SPDX-License-Identifier: Apache-2.0
#include "benchmark.h"
#include <cstring>
#include "hwy/aligned_allocator.h"
#include "hwy/contrib/algo/copy-inl.h"
#include "hwy/contrib/algo/find-inl.h"
template <class T>
void BenchType(const char* name) {
  const hn::ScalableTag<T> d;
  auto input = hwy::AllocateAligned<T>(257 + hn::Lanes(d));
  auto original = hwy::AllocateAligned<T>(257 + hn::Lanes(d));
  auto output = hwy::AllocateAligned<T>(257 + hn::Lanes(d));
  HWY_ASSERT(input && original && output);
  uint8_t percent[257];
  HighwaySetPhase(HighwayPhase::Inputs);
  for (size_t i = 0; i < 257; ++i) percent[i] = static_cast<uint8_t>(Mix(i) % 100);
  HighwaySetPhase(HighwayPhase::Setup);
  for (size_t n : kSizes) {
    for (size_t run : {size_t{1}, size_t{3}, size_t{17}}) {
      HighwaySetPhase(HighwayPhase::Inputs);
      for (size_t i = 0; i < n; ++i) original[i] = static_cast<T>((i / run) % 2);
      HighwaySetPhase(HighwayPhase::Setup);
      const auto prepare = [&] { memcpy(input.get(), original.get(), n * sizeof(T)); };
      Measure(name, n, prepare, [&] { const auto count = hn::Unique(d, input.get(), n); Consume(count); });
      Measure("all-unique", n, [&] { const auto result = hn::AllUnique(d, original.get(), n); Consume(result); });
    }
    for (uint32_t density : {0u, 10u, 50u, 90u, 100u}) {
      HighwaySetPhase(HighwayPhase::Inputs);
      for (size_t i = 0; i < n; ++i) input[i] = static_cast<T>(percent[i] < density);
      HighwaySetPhase(HighwayPhase::Setup);
      Measure("copy-if", n, [&] {
        const auto end = hn::CopyIf(d, input.get(), n, output.get(), [](auto tag, auto value) {
          return hn::Eq(value, hn::Set(tag, T{1}));
        });
        Consume(end); Consume(output.get());
      });
    }
  }
}
void RunBenchmarks() {
  BenchType<uint8_t>("unique.u8");
  BenchType<int16_t>("unique.i16");
  BenchType<int32_t>("unique.i32");
  BenchType<int64_t>("unique.i64");
}
