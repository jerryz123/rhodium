// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

using Table = std::array<std::array<unsigned, 4>, 8>;
Table base_model, history_model, choice_model;

void cold_model() {
  for (auto *table : {&base_model, &history_model, &choice_model})
    for (auto &row : *table) row.fill(1);
}
void check_rows() {
  for (unsigned r = 0; r < 8; ++r) {
    base_row = r;
    gshare_row = (r + 3) % 8;
    choice_row = (r + 5) % 8;
    prefix_gshare_row = (r + 2) % 8;
    prefix_choice_row = (r + 4) % 8;
    eval();
    for (unsigned b = 0; b < 4; ++b) {
      CHECK(base_taken[b] == (base_model[r][b] >= 2));
      CHECK(gshare_taken[b] == (history_model[(r + 3) % 8][b] >= 2));
      CHECK(use_gshare[b] == (choice_model[(r + 5) % 8][b] >= 2));
    }
    CHECK(prefix_gshare_taken == (history_model[(r + 2) % 8][3] >= 2));
    CHECK(prefix_use_gshare == (choice_model[(r + 4) % 8][3] >= 2));
  }
}
void step(unsigned &counter, bool increment) {
  counter = increment ? std::min(counter + 1, 3u) : (counter ? counter - 1 : 0);
}
void train(unsigned base_index, unsigned history_index, bool base_prediction,
           bool history_prediction, bool actual) {
  update_in = {1, {static_cast<std::uint8_t>(history_index), static_cast<std::uint8_t>(base_index), base_prediction, history_prediction, actual}};
  tick_model();
  update_in = {};
  step(base_model[base_index / 4][base_index % 4], actual);
  step(history_model[history_index / 4][history_index % 4], actual);
  if (base_prediction != history_prediction)
    step(choice_model[base_index / 4][base_index % 4], history_prediction == actual);
  check_rows();
}
int main() {
  return run_test([] {
    reset = 1;
    update_in = {};
    clear_in = {};
    cold_model();
    tick_model(); tick_model();
    reset = 0;
    check_rows();
    // Changing history contexts must not fragment the bimodal loop counter.
    for (unsigned iteration = 0; iteration < 8; ++iteration)
      train(7, iteration * 4 + 3, iteration != 0, false, true);
    CHECK(base_model[1][3] == 3 && choice_model[1][3] == 0);
    // Learn history preference, then learn the opposite; current counters saturate.
    for (unsigned i = 0; i < 6; ++i) train(7, 19, false, true, true);
    CHECK(choice_model[1][3] == 3);
    for (unsigned i = 0; i < 6; ++i) train(7, 19, true, false, true);
    CHECK(choice_model[1][3] == 0);
    // Agreement must not move the chooser, even when both predictions are wrong.
    for (unsigned i = 0; i < 6; ++i) train(7, 19, false, false, true);
    CHECK(choice_model[1][3] == 0);
    // Delayed predictions remain authoritative after intervening table updates.
    train(7, 19, false, true, false);
    CHECK(choice_model[1][3] == 0);
    for (unsigned pass = 0; pass < 12; ++pass)
      for (unsigned index = 0; index < 32; ++index)
        train(index, (index * 13 + pass) % 32, pass % 2, (index + pass) % 3 == 0,
              (index ^ pass) % 5 != 0);
    // Clear masks lookup immediately, wins over training, and leaves no stale rows.
    clear_in.pvalid = 1;
    update_in = {1, {7, 7, false, true, true}};
    cold_model();
    check_rows();
    tick_model();
    update_in = {};
    clear_in = {};
    check_rows();
    train(7, 19, false, true, true);
    reset = 1;
    tick_model();
    reset = 0;
    cold_model();
    check_rows();
  });
}
