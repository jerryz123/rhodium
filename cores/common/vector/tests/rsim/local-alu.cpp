// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

using Word = decltype(response_in.pbits.pdata);
constexpr unsigned sew = sizeof(Word) == 8 ? 3 : 2;
constexpr unsigned memory_width = sizeof(Word) == 8 ? 7 : 6;
constexpr unsigned unmasked = 1u << 25;
constexpr unsigned add = unmasked | (2u << 20) | (4u << 15) | (20u << 7) | 0x57;
constexpr unsigned extract = (16u << 26) | unmasked | (20u << 20) | (2u << 12) | (5u << 7) | 0x57;
unsigned retired_count, scalar_count;
Word expected;

static void step() {
  eval();
  CHECK(test_cycles < 500);
  if (retired) ++retired_count;
  if (scalar_result_out.pvalid) {
    CHECK(scalar_result_out.pbits.paddress == 5 && scalar_result_out.pbits.pdata == expected);
    ++scalar_count;
  }
  tick_model();
  eval();
}

static void launch(unsigned insn) {
  instruction = insn;
  request_valid = 1;
  eval();
  while (!request_ready) step();
  step();
  request_valid = 0;
  eval();
}

static void drain() {
  while (active) step();
}

static void fill(unsigned vd, unsigned value) {
  launch(0x5e003057 | (value << 15) | (vd << 7));
  drain();
}

static void add_and_consume() {
  unsigned before = retired_count, results = scalar_count;
  launch(add);
  launch(extract); // Accepted on the producer's final read, before it writes v20.
  CHECK(issued && !sequencing_finished && !retired);
  step(); // Producer issued. The consumer cannot have read the old v20.
  CHECK(!issued && !retired); // Producer is in EX, not yet retired.
  step();
  while (scalar_count == results) step();
  CHECK(retired_count <= before + 1);
}

int main() {
  return run_test([] {
    reset = issue_ready = 1;
    step();
    reset = 0;
    vl = 1;
    vtype = sew << 3;
    fill(2, 3);
    fill(4, 4);
    fill(20, 11);
    expected = 7;
    auto start = retired_count;
    add_and_consume(); // With no older instruction, an early retire is visible directly.
    drain();
    CHECK(retired_count == start + 2);

    start = retired_count;
    launch(add);
    while (!issued) step();
    step(); // The final beat is in EX, but its owner is not yet durable.
    cancel = 1;
    step();
    cancel = 0;
    eval();
    drain();
    CHECK(retired_count == start);

    fill(20, 12);
    start = retired_count;
    vl = 8;
    vtype = (sew << 3) | 2; // LMUL=4: one instruction owns all eight slow-load tags.
    slow = 1;
    launch(unmasked | (memory_width << 12) | (8u << 7) | 0x07);
    std::array<unsigned, 8> tags{};
    unsigned live = 0;
    for (unsigned lane = 0; lane < tags.size(); ++lane) {
      while (!resolution_out.pvalid) step();
      tags[lane] = resolution_out.pbits.pcompletion_utag;
      CHECK(!(live & (1u << tags[lane])) && resolution_out.pbits.penabled);
      live |= 1u << tags[lane];
      step();
    }
    CHECK(live == 255);
    slow = 0;
    vl = 1;
    vtype = sew << 3;
    add_and_consume(); // Neither beat can borrow a tag: all eight replies are still withheld.
    CHECK(active && retired_count == start);

    for (unsigned i = tags.size(); i-- > 0;) {
      response_in.pvalid = 1;
      response_in.pbits.ptag = tags[i];
      response_in.pbits.pdata = 0x30 + i;
      eval();
      while (!response_out.pready) step();
      CHECK(retired_count == start);
      step();
    }
    response_in = {};
    drain();
    CHECK(retired_count == start + 3);

    // Check that the slotless writes neither completed nor changed an aliased load slot.
    vl = 8;
    vtype = (sew << 3) | 2;
    launch(unmasked | (memory_width << 12) | (8u << 7) | 0x27);
    for (unsigned lane = 0; lane < tags.size(); ++lane) {
      while (!resolution_out.pvalid) step();
      CHECK(resolution_out.pbits.pstore && resolution_out.pbits.pstore_udata == 0x30 + lane);
      step();
    }
    drain();
  });
}
