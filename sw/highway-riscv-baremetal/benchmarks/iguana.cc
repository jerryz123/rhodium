// Runs bounded Highway Iguana compression and vector decompression on mixed literal/repeated inputs.
// SPDX-License-Identifier: Apache-2.0
#include "benchmark.h"
#include "hwy/aligned_allocator.h"
#include "hwy/contrib/iguana/iguana-inl.h"
void RunBenchmarks() {
  hwy::ThreadPool pool(0);
  hwy::iguana::IguanaWorkspace encoder, decoder;
  for (size_t n : {size_t{128}, size_t{1024}}) {
    auto input = hwy::AllocateAligned<uint8_t>(n);
    auto compressed = hwy::AllocateAligned<uint8_t>(hwy::iguana::MaxCompressedSize(n));
    auto output = hwy::AllocateAligned<uint8_t>(n);
    HWY_ASSERT(input && compressed && output);
    HighwaySetPhase(HighwayPhase::Inputs);
    for (size_t i = 0; i < n; ++i)
      input[i] = static_cast<uint8_t>((i % 128 < 96) ? (i % 17) : Mix(i));
    HighwaySetPhase(HighwayPhase::Setup);
    HWY_ASSERT(encoder.Reserve(n, 1) && decoder.Reserve(n, 1));
    const hwy::Span<const uint8_t> in(input.get(), n);
    const hwy::Span<uint8_t> out(compressed.get(), hwy::iguana::MaxCompressedSize(n));
    size_t compressed_size = 0;
    Measure("iguana.compress", n, [&] {
      compressed_size = hwy::iguana::Compress(in, out, encoder, pool);
      HWY_ASSERT(compressed_size > 0);
      Consume(compressed.get());
    });
    Measure("iguana.decompress", n, [&] {
      const auto written = hn::DecompressStatic(
          hwy::Span<const uint8_t>(compressed.get(), compressed_size),
          hwy::Span<uint8_t>(output.get(), n), decoder, pool);
      HWY_ASSERT(written == n);
      Consume(output.get());
    });
  }
}
