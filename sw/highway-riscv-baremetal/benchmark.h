// Shares bounded input sizes, anti-elision, and single-invocation measurement for native RVV workloads.
// SPDX-License-Identifier: Apache-2.0
#ifndef RHODIUM_HIGHWAY_BENCHMARK_H_
#define RHODIUM_HIGHWAY_BENCHMARK_H_
#include <cstddef>
#include <cstdint>
#include "hwy/highway.h"
static_assert(HWY_TARGET == HWY_RVV, "Highway benchmarks require native RVV");
namespace hn = hwy::HWY_NAMESPACE;
struct HighwayStamp { uint64_t cycles, instructions; };
enum class HighwayPhase {
  Setup, Inputs, Kernel, Reporting,
  PhastBuild, ShardmulBuild, Cuckoo2x2Init, CuckooBuild, PackedInit, PackedConvert,
  Count
};
extern "C" HighwayStamp highway_begin();
HighwayStamp HighwaySetPhase(HighwayPhase phase);
extern "C" uint64_t highway_end(const char* name, size_t size, HighwayStamp start);
inline constexpr size_t kSizes[] = {1, 7, 16, 31, 64, 257};
template <class T> inline void Consume(const T& value) {
  asm volatile("" : : "g"(&value) : "memory");
}
template <class Prepare, class Kernel>
void Measure(const char* name, size_t size, const Prepare& prepare, const Kernel& kernel) {
  HighwaySetPhase(HighwayPhase::Inputs);
  prepare();
  const auto start = HighwaySetPhase(HighwayPhase::Kernel);
  kernel();
  highway_end(name, size, start);
}
template <class Kernel>
void Measure(const char* name, size_t size, const Kernel& kernel) {
  const auto start = HighwaySetPhase(HighwayPhase::Kernel);
  kernel();
  highway_end(name, size, start);
}
inline uint32_t Mix(uint32_t index) {
  uint32_t value = index + 0x9e3779b9u;
  value = (value ^ (value >> 16)) * 0x85ebca6bu;
  value = (value ^ (value >> 13)) * 0xc2b2ae35u;
  return value ^ (value >> 16);
}
void RunBenchmarks();
#endif
