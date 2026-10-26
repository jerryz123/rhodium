// Runs Highway VQSort across key types, orders, distributions, and three representative input sizes.
// SPDX-License-Identifier: Apache-2.0
#include "benchmark.h"
#include <cstring>
#include "hwy/aligned_allocator.h"
#include "hwy/contrib/sort/vqsort.h"
template <class T> void BenchType(const char* name) {
  auto keys = hwy::AllocateAligned<T>(129);
  auto original = hwy::AllocateAligned<T>(129);
  HWY_ASSERT(keys && original);
  // Keep a singleton, an uneven vector tail, and the previous largest case so
  // the existing partition paths remain represented for narrower lane counts.
  for (size_t n : {size_t{1}, size_t{31}, size_t{129}}) {
    for (unsigned pattern = 0; pattern < 4; ++pattern) {
      HighwaySetPhase(HighwayPhase::Inputs);
      for (size_t i = 0; i < n; ++i) {
          const uint32_t key = pattern == 0 ? Mix(i) % 4096 :
                               pattern == 1 ? i : pattern == 2 ? n - i : i % 3;
          original[i] = hwy::ConvertScalarTo<T>(key);
      }
      HighwaySetPhase(HighwayPhase::Setup);
      const auto prepare = [&] { memcpy(keys.get(), original.get(), n * sizeof(T)); };
      Measure(name, n, prepare, [&] { hwy::VQSort(keys.get(), n, hwy::SortAscending()); Consume(keys.get()); });
      Measure(name, n, prepare, [&] { hwy::VQSort(keys.get(), n, hwy::SortDescending()); Consume(keys.get()); });
    }
  }
}
void RunBenchmarks() {
  BenchType<int16_t>("vqsort.i16"); BenchType<uint16_t>("vqsort.u16");
  BenchType<int32_t>("vqsort.i32"); BenchType<uint32_t>("vqsort.u32");
  BenchType<int64_t>("vqsort.i64"); BenchType<uint64_t>("vqsort.u64");
  BenchType<hwy::float16_t>("vqsort.f16");
  BenchType<float>("vqsort.f32"); BenchType<double>("vqsort.f64");
}
