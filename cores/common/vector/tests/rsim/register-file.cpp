// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

using Word = decltype(write_in.pbits.pdata);
constexpr unsigned word_bits = sizeof(Word) * 8;
constexpr unsigned rows = 128 / word_bits;
constexpr unsigned depth = 32 * rows;
std::array<Word, depth> reference{};
std::array<Word, 4> previous_data{};
std::array<bool, 4> previous_valid{};
std::uint64_t rng = UINT64_C(0x146b35e7da08c912);

Word random_word() {
  rng ^= rng << 13;
  rng ^= rng >> 7;
  rng ^= rng << 17;
  return Word(rng);
}

void check_response(const std::array<bool, 4> &valid,
                    const std::array<Word, 4> &data) {
  const std::array actual_valid = {bool(results_0_out.pvalid),
                                   bool(results_1_out.pvalid),
                                   bool(results_2_out.pvalid),
                                   bool(mask_result_out.pvalid)};
  const std::array actual_data = {results_0_out.pbits, results_1_out.pbits,
                                  results_2_out.pbits, mask_result_out.pbits};
  CHECK(actual_valid == valid);
  for (unsigned port = 0; port < 4; ++port)
    if (valid[port]) CHECK(actual_data[port] == data[port]);
}

void step() {
  eval();
  // New requests/writes cannot alter the previous registered response.
  check_response(previous_valid, previous_data);
  if (!reset && write_in.pvalid) {
    auto &word = reference[write_in.pbits.paddress];
    word = (word & ~write_in.pbits.pmask) |
           (write_in.pbits.pdata & write_in.pbits.pmask);
  }
  previous_valid = {bool(reads_0_in.pvalid && !reset),
                    bool(reads_1_in.pvalid && !reset),
                    bool(reads_2_in.pvalid && !reset),
                    bool(mask_read_in.pvalid && !reset)};
  previous_data = {reference[reads_0_in.pbits], reference[reads_1_in.pbits],
                   reference[reads_2_in.pbits], reference[mask_read_in.pbits]};
  tick_model();
  eval();
  check_response(previous_valid, previous_data);
}

int main() {
  return run_test([] {
    reset = 1;
    tick_model();
    reset = 0;
    // Architectural storage is uninitialized: write every row before reading.
    write_in.pvalid = 1;
    write_in.pbits.pmask = Word(-1);
    for (unsigned row = 0; row < depth; ++row) {
      write_in.pbits.paddress = row;
      write_in.pbits.pdata = random_word();
      step();
    }

    for (unsigned cycle = 0; cycle < 4096; ++cycle) {
      const unsigned mask_row = cycle % rows;
      // First all four ports collide on v0; then other registers with the
      // same chunk index must neither forward into nor overwrite the mask.
      const unsigned mode = (cycle / rows) % 4;
      const unsigned destination = mode == 0 ? mask_row :
          (mode == 1 ? (1 + cycle % 31) * rows + mask_row : random_word() % depth);
      reads_0_in.pbits = destination;
      reads_1_in.pbits = mode == 0 ? mask_row : random_word() % depth;
      reads_2_in.pbits = mask_row;
      mask_read_in.pbits = mask_row;
      reads_0_in.pvalid = mode < 2 || cycle % 3;
      reads_1_in.pvalid = mode < 2 || cycle % 5;
      reads_2_in.pvalid = mode < 2 || cycle % 7;
      mask_read_in.pvalid = mode < 2 || cycle % 11;
      write_in.pvalid = mode < 2 || cycle % 13;
      write_in.pbits.paddress = destination;
      write_in.pbits.pdata = random_word();
      switch ((cycle / (4 * rows)) % 4) {
      case 0: write_in.pbits.pmask = Word(1) << (cycle % word_bits); break;
      case 1: write_in.pbits.pmask = random_word(); break;
      case 2: write_in.pbits.pmask = Word(-1); break;
      case 3: write_in.pbits.pmask = 0; break;
      }
      // Reset suppresses the offered write and clears each result valid,
      // but the next cycle must still observe previously written storage.
      reset = cycle == 2048;
      step();
    }
    write_in.pvalid = 0;
    reads_0_in.pvalid = reads_1_in.pvalid = reads_2_in.pvalid = mask_read_in.pvalid = 1;
    for (unsigned row = 0; row < depth; ++row) {
      reads_0_in.pbits = reads_1_in.pbits = reads_2_in.pbits = row;
      mask_read_in.pbits = row % rows;
      step();
    }
  });
}
