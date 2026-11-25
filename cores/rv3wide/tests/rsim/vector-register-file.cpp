// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <random>

using Word = std::remove_cvref_t<decltype(write_in.pbits.pdata)>;
template<class T> T random_word(std::mt19937_64& gen) {
  if constexpr (std::is_integral_v<T>) return static_cast<T>(gen());
  else { T x{}; for (auto& v : x.words) v = gen(); return x; }
}

int main() {
  return run_test([] {
    constexpr unsigned per_reg = 2; // VLEN = 2 * DLEN for both geometries.
    constexpr unsigned depth = 32 * per_reg;
    std::array<Word, depth> model{};
    std::mt19937_64 rng(128);
    reset = 1; tick_model(); reset = 0;
    for (unsigned i = 0; i < depth * 7; ++i) {
      const bool init = i < depth;
      const unsigned addr = i % depth;
      // Include v0 writes and a non-v0 write to the same physical row offset.
      const unsigned wa = init ? addr : i % 3 == 0 ? i % per_reg : addr;
      write_in.pvalid = init || i % 8 != 0;
      write_in.pbits.paddress = wa;
      write_in.pbits.pdata = random_word<Word>(rng);
      write_in.pbits.pmask = init ? ~Word{} : i % 7 == 0 ? Word{} : random_word<Word>(rng);
      reset = !init && i == depth * 3 + 1;
      if (write_in.pvalid && !reset)
        model[wa] = (model[wa] & ~write_in.pbits.pmask) | (write_in.pbits.pdata & write_in.pbits.pmask);
      const unsigned b = (wa + 1) % depth, c = depth - 1 - addr, mask = wa % per_reg;
      reads_0_in.pvalid = 1; reads_0_in.pbits = wa;
      reads_1_in.pvalid = i % 5 != 1; reads_1_in.pbits = b;
      reads_2_in.pvalid = i % 7 != 2; reads_2_in.pbits = c;
      mask_read_in.pvalid = i % 9 != 2; mask_read_in.pbits = mask;
      tick_model();
      // Changing the next inputs cannot affect the just-sampled read.
      write_in.pbits.pdata = random_word<Word>(rng);
      reads_0_in.pbits = c; mask_read_in.pbits = (mask + 1) % per_reg;
      eval();
      if (reset) { CHECK(!results_0_out.pvalid && !mask_result_out.pvalid); continue; }
      CHECK(results_0_out.pvalid && (results_0_out.pbits == model[wa]));
      CHECK(results_1_out.pvalid == reads_1_in.pvalid);
      CHECK(results_2_out.pvalid == reads_2_in.pvalid);
      CHECK(mask_result_out.pvalid == mask_read_in.pvalid);
      if (!init && reads_1_in.pvalid) CHECK((results_1_out.pbits == model[b]));
      if (!init && reads_2_in.pvalid) CHECK((results_2_out.pbits == model[c]));
      if (!init && mask_read_in.pvalid) CHECK((mask_result_out.pbits == model[mask]));
    }
  });
}
