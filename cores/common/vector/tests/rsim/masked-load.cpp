// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

using Word = decltype(response_in.pbits.pdata);
constexpr unsigned bytes = sizeof(Word), word_sew = bytes == 8 ? 3 : 2;
constexpr unsigned word_eew = bytes == 8 ? 7 : 6;

static void step() {
  CHECK(test_cycles < 500);
  eval();
  tick_model();
  eval();
}

static void launch(unsigned insn, unsigned base) {
  instruction = insn;
  scalar = base;
  request_valid = 1;
  eval();
  while (!request_ready) step();
  step();
  request_valid = 0;
}

static auto pending(unsigned address) {
  eval();
  while (!resolution_out.pvalid) step();
  CHECK(resolution_out.pbits.paddress == address);
  return resolution_out.pbits;
}

static void decide() {
  decision_hold = 0;
  step();
  decision_hold = 1;
}

static void drain() {
  eval();
  while (active) step();
}

int main() {
  return run_test([] {
    reset = issue_ready = decision_hold = 1;
    step();
    reset = 0;
    vl = 1;
    vtype = word_sew << 3;
    constexpr unsigned unmasked = 1u << 25;
    constexpr unsigned load_word = unmasked | (word_eew << 12) | 0x07;
    const Word original = static_cast<Word>(UINT64_C(0xf1e2d3c4b5a69788));
    const Word mask = 2u | (1u << (bytes - 1));
    for (unsigned vd : {0u, 8u}) {
      launch(load_word | (vd << 7), 0x100);
      CHECK(pending(0x100).penabled);
      hit_data = vd == 0 ? mask : original;
      decide();
      drain();
    }

    // Masked vle8.v v8,(base),v0.t: reject byte 1 once, then accept it slow.
    // Let the last byte hit while the replayed byte's completion is outstanding.
    vl = bytes;
    vtype = 0;
    launch((8u << 7) | 0x07, 0x200);
    unsigned delayed_tag = 0;
    for (unsigned lane = 0; lane < bytes; ++lane) {
      auto token = pending(0x200 + lane);
      CHECK(!token.pprecertified && !token.ppacked && !token.pstore);
      CHECK(token.penabled == ((mask >> lane) & 1));
      if (lane == 1) {
        retry = 1;
        decide();
        retry = 0;
        token = pending(0x201);
        CHECK(token.penabled && !token.ppacked && !token.pstore);
        delayed_tag = token.pcompletion_utag;
        slow = 1;
      }
      hit_data = ~Word(0) - 0x2c; // Low byte 0xd3; poison upper bytes.
      decide();
      slow = 0;
    }

    for (unsigned i = 0; i < 8; ++i) {
      CHECK(active && !resolution_out.pvalid);
      step();
    }
    response_in.pvalid = 1;
    response_in.pbits.ptag = delayed_tag;
    response_in.pbits.pdata = ~Word(0) - 0xa5; // Low byte 0x5a.
    eval();
    while (!response_out.pready) step();
    step();
    response_in = {};
    drain();

    vl = 1;
    vtype = word_sew << 3;
    launch(unmasked | (word_eew << 12) | (8u << 7) | 0x27, 0x300);
    const auto readback = pending(0x300);
    CHECK(readback.pstore && readback.penabled);
    for (unsigned lane = 0; lane < bytes; ++lane) {
      const auto expected = lane == 1 ? 0x5a : lane == bytes - 1 ? 0xd3 : (original >> (8 * lane)) & 0xff;
      CHECK(((readback.pstore_udata >> (8 * lane)) & 0xff) == expected);
    }
    decide();
    drain();
  });
}
