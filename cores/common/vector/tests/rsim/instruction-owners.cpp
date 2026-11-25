// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

constexpr unsigned unmasked = 1u << 25;
constexpr unsigned load = unmasked | (7u << 12) | (8u << 7) | 0x07;
constexpr unsigned add = unmasked | (2u << 20) | (4u << 15) | 0x57;
unsigned retires, issues;

static void step() {
  eval();
  CHECK(test_cycles < 600);
  retires += retired;
  issues += issued;
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

int main() {
  return run_test([] {
    reset = issue_ready = 1;
    step();
    reset = 0;
    for (unsigned round = 0; round < 2; ++round) {
      const unsigned initial = retires;
      vl = 8;
      vtype = (3u << 3) | 2; // Eight RV64 beats, one LMUL4 load owner.
      slow = 1;
      launch(load);
      std::array<unsigned, 8> tags{};
      unsigned seen = 0;
      for (auto& tag : tags) {
        while (!resolution_out.pvalid) step();
        tag = resolution_out.pbits.pcompletion_utag;
        CHECK(resolution_out.pbits.penabled && !(seen & (1u << tag)));
        seen |= 1u << tag;
        step();
      }
      CHECK(seen == 255);

      slow = 0;
      vl = 1;
      vtype = 3u << 3;
      const unsigned start = issues;
      for (unsigned vd : {16u, 17u, 18u}) launch(add | (vd << 7));
      while (issues != start + 3) step();
      instruction = add | (19u << 7);
      request_valid = 1;
      eval();
      for (unsigned i = tags.size(); i-- > 0;) {
        response_in.pvalid = 1;
        response_in.pbits.ptag = tags[i];
        response_in.pbits.pdata = i + 1;
        eval();
        while (!response_out.pready) {
          CHECK(!request_ready && retires == initial);
          step();
        }
        CHECK(!request_ready && retires == initial);
        step();
      }
      response_in = {};
      eval();
      while (!request_ready) step();
      CHECK(retires > initial);
      step();
      request_valid = 0;
      eval();
      while (active) step();
      CHECK(retires == initial + 5 && issues == start + 4);
    }
  });
}
