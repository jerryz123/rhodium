// Exercises Highway's keyed LeMac and fingerprint kernels for both stripe widths and boundary-sized inputs.
// SPDX-License-Identifier: Apache-2.0
#include "benchmark.h"
#include "hwy/contrib/hash/lemac-inl.h"
template <size_t Stripes> void BenchStripes() {
  uint8_t input[1024], key_bytes[hwy::LeMac::kKeyBytes], nonce[hwy::LeMac::kNonceBytes] = {1};
  uint8_t output[hwy::LeMac::kHashBytes];
  HighwaySetPhase(HighwayPhase::Inputs);
  for (size_t i = 0; i < sizeof(input); ++i) input[i] = static_cast<uint8_t>(Mix(i));
  for (size_t i = 0; i < sizeof(key_bytes); ++i) key_bytes[i] = static_cast<uint8_t>(i * 17 + 1);
  HighwaySetPhase(HighwayPhase::Setup);
  const auto key = hn::LeMacMakeKey<Stripes>(key_bytes);
  const auto fingerprint = hn::LeMacMakeFingerprintKey<Stripes>();
  for (size_t n : {size_t{0}, size_t{1}, size_t{16}, size_t{63}, size_t{256}, size_t{1024}}) {
    Measure("lemac", n, [&] { hn::LeMacHash(key, nonce, input, n, output); Consume(output); });
    Measure("fingerprint", n, [&] { hn::LeMacHash(fingerprint, nonce, input, n, output); Consume(output); });
  }
}
void RunBenchmarks() { BenchStripes<1>(); BenchStripes<2>(); }
