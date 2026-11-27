// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <limits>

using Word = decltype(request_in.pbits.pdescriptor.pstate.pvstart);
constexpr Word high = Word(1) << (std::numeric_limits<Word>::digits - 1);

enum class Move { FromGpr, FromFpr, ToGpr, ToFpr };
// VectorSequence and VectorResultKind encodings from decode.rhdl.
enum Sequence { Extract = 3, Insert = 4 };
enum ResultKind { Elements = 0, Gpr = 2, Fpr = 3 };
struct Case { Move move; Word vstart, vl; bool empty; };
constexpr Case cases[] = {
    {Move::FromGpr, 1, 2, false},
    {Move::FromGpr, high | 1, 2, true},   // Same low bits; opposite answer.
    {Move::FromFpr, 2, high | 1, false},  // Truncating vl would suppress the move.
    {Move::FromFpr, 2, 2, true},
    {Move::ToGpr, high, 0, false},
    {Move::FromGpr, 0, 0, true},
    {Move::ToFpr, 0, 0, false},
};

static void offer(unsigned i) {
  request_in = {};
  if (i == std::size(cases)) return;
  const auto &c = cases[i];
  request_in.pvalid = 1;
  auto &d = request_in.pbits.pdescriptor;
  d.ppc = 4 * i;
  d.pstate.pvstart = c.vstart;
  d.pstate.pvl = c.vl;
  const bool extract = c.move == Move::ToGpr || c.move == Move::ToFpr;
  d.pcontrol.psequence = extract ? Extract : Insert;
  d.pcontrol.pfloating_upoint = c.move == Move::FromFpr || c.move == Move::ToFpr;
  d.pcontrol.pinteger = !d.pcontrol.pfloating_upoint;
  d.pcontrol.presult_ukind = c.move == Move::ToGpr ? Gpr : c.move == Move::ToFpr ? Fpr : Elements;
}

int main() {
  return run_test([] {
    reset = 1;
    tick_model();
    reset = 0;
    offer(0);
    eval();
    CHECK(request_out.pready);
    tick_model();

    for (unsigned i = 0; i < std::size(cases); ++i) {
      offer(i + 1); // Hold the successor valid while the resident tail is stalled.
      for (unsigned cycle = 0; cycle < 3; ++cycle) {
        bool release = cycle == 2;
        sequence_request_in.pready = release;
        eval();
        CHECK(sequence_request_out.pvalid);
        const auto &read = sequence_request_out.pbits;
        const auto &beat = read.pcontext.pbeat_ucontrol;
        CHECK(read.pcontext.pinstruction.pdescriptor.ppc == 4 * i);
        CHECK(beat.plast && bool(beat.pempty) == cases[i].empty);
        CHECK(bool(read.penables & 1) == !cases[i].empty);
        CHECK(bool(request_out.pready) == release); // Tail and successor transfer together.
        tick_model();
      }
    }
    eval();
    CHECK(!sequence_request_out.pvalid && !active);
  });
}
