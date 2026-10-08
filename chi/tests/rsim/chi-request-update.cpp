// Checks generic stripe edge cases and metadata-transparent CHI address
// projection.
// SPDX-License-Identifier: Apache-2.0
#include "request.hpp"
#include "test.hpp"
#include <random>
std::mt19937_64 random_word(0x217afe);

int main() {
  return run_test([] {
    valid = 0, ready = 0;
    int global_offset, local_offset;
    for (int address = 0; address < 4096; address++) {
      offset = ((address)&low_mask(12));
      eval();
      CHECK(dense_single == offset &&
            dense_four ==
                (((address / 256) * 64 + address % 64) & low_mask(12)) &&
            dense_byte == ((address / 4) & low_mask(12)) &&
            dense_stripe_only == ((address % 1024) & low_mask(12)));
    }
    for (int i = 0; i < 256; i++) {
      fill_request(source, [] { return random_word(); }, 16, 52);
      global_offset = (i / 4) * 256 + (i % 4) * 16;
      local_offset = (i / 4) * 64 + (i % 4) * 16;
      source.paddress = UINT64_C(2147483776) + ((global_offset)&low_mask(52));
      valid = (i & 1);
      ready = ((i >> 1) & 1);
      eval();
      auto expected = source;
      expected.paddress = UINT64_C(4096) + ((local_offset)&low_mask(52));
      if (!same_request(projected, expected))
        fail(1, "projector changed request metadata");
      if (result_valid != valid || source_ready != ready)
        fail(1, "projector handshake");
    }
  });
}
