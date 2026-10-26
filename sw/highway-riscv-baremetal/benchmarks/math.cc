// Runs every Highway benchmark math function/tag at eight samples spanning its original input range.
// SPDX-License-Identifier: Apache-2.0
#include "benchmark.h"
#include "hwy/aligned_allocator.h"
#include "hwy/contrib/math/math-inl.h"
#include "hwy/contrib/math/fast_math-inl.h"
template <class D, class Function>
void BenchFunction(D d, hn::TFromD<D>* output, const char* name, double low, double step, const Function& function) {
  using T = hn::TFromD<D>;
  constexpr size_t kSamples = 8;
  T inputs[kSamples];
  HighwaySetPhase(HighwayPhase::Inputs);
  // Subsample the original 16-point grid, retaining both endpoints and
  // representative interior values in the exact original typed domain.
  for (size_t i = 0; i < kSamples; ++i) {
    const size_t index = (i * 15 + 3) / 7;
    inputs[i] = static_cast<T>(low + static_cast<double>(index) * step);
  }
  HighwaySetPhase(HighwayPhase::Setup);
  Measure(name, hn::Lanes(d), [&] {
    for (size_t i = 0; i < kSamples; ++i) {
      const auto input = hn::Set(d, inputs[i]);
      const auto result = function(d, input);
      hn::Store(result, d, output);
      Consume(output);
    }
  });
}
template <class D>
void BenchTag(D d) {
  auto output = hwy::AllocateAligned<hn::TFromD<D>>(hn::Lanes(d));
  HWY_ASSERT(output);
#define MATH(NAME, LOW, STEP) BenchFunction(d, output.get(), #NAME, LOW, STEP, [](auto tag, auto value) { return hn::Call##NAME(tag, value); })
  MATH(Exp, -10.0, 20.0 / 15.0);
  MATH(FastExp, -10.0, 20.0 / 15.0);
  MATH(FastExpNormal, -10.0, 20.0 / 15.0);
  MATH(Exp2, -10.0, 20.0 / 15.0);
  MATH(FastExp2, -10.0, 20.0 / 15.0);
  MATH(FastExp2Normal, -10.0, 20.0 / 15.0);
  MATH(FastExpMinusOrZero, -10.0, 10.0 / 15.0);
  MATH(Log, 0.1, 1.0);
  MATH(FastLog, 0.1, 1.0);
  MATH(FastLogPositiveNormal, 0.1, 1.0);
  MATH(Log2, 0.1, 1.0);
  MATH(FastLog2, 0.1, 1.0);
  MATH(FastLog2PositiveNormal, 0.1, 1.0);
  MATH(Log10, 0.1, 1.0);
  MATH(FastLog10, 0.1, 1.0);
  MATH(FastLog10PositiveNormal, 0.1, 1.0);
  MATH(Log1p, 0.1, 1.0);
  MATH(FastLog1p, 0.1, 1.0);
  MATH(FastLog1pPositiveNormal, 0.1, 1.0);
  MATH(Atan, -10.0, 20.0 / 15.0);
  MATH(FastAtan, -10.0, 20.0 / 15.0);
  MATH(FastAtanPositive, 0.1, 20.0 / 15.0);
  MATH(Tanh, -10.0, 20.0 / 15.0);
  MATH(FastTanh, -10.0, 20.0 / 15.0);
  MATH(FastTan, -1.57, 3.14 / 15.0);
#undef MATH
  BenchFunction(d, output.get(), "Tan", -1.57, 3.14 / 15.0, [](auto tag, auto value) { return hn::Tan(tag, value); });
  BenchFunction(d, output.get(), "Atan2", -10.0, 20.0 / 15.0, [](auto tag, auto value) {
    return hn::CallAtan2(tag, value, hn::Set(tag, hn::TFromD<D>{0.75}));
  });
  BenchFunction(d, output.get(), "FastAtan2", -10.0, 20.0 / 15.0, [](auto tag, auto value) {
    return hn::CallFastAtan2(tag, value, hn::Set(tag, hn::TFromD<D>{0.75}));
  });
}
template <class T> void BenchType() {
  BenchTag(hn::CappedTag<T, 1>());
  BenchTag(hn::ScalableTag<T>());
  BenchTag(hn::ScalableTag<T, 1>());
}
void RunBenchmarks() { BenchType<float>(); BenchType<double>(); }
