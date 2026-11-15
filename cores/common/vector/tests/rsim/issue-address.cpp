// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

using Word = decltype(request_in.pbits.pdescriptor.pscalar);
using Feedback = decltype(feedback_in.pbits);
constexpr unsigned bytes = sizeof(Word), start = 2, stride = 13;

struct Macro {
  bool packed, certified;
  Word base;
  unsigned vl;

  void offer() const {
    request_in = {};
    packed_memory = packed;
    request_in.pvalid = 1;
    auto &d = request_in.pbits.pdescriptor;
    d.ppc = base & ~Word(3);
    d.pscalar = base;
    d.pstride = stride;
    d.pinstruction = packed ? 0 : (1u << 20); // Nonzero stride register, not the rs2=x0 splat.
    d.pcontrol.pmemory = 1;
    d.pcontrol.pmemory_uaddressing = packed ? 0 : 1; // Unit-stride or strided
    d.pstate.pvtype = 2; // SEW=8, LMUL=4
    d.pstate.pvl = vl;
    d.pstate.pvstart = start;
    d.pprecertified = certified;
    d.pcertificate.prange.plast = UINT64_MAX;
  }

  Feedback check_beat(unsigned i) const {
    const Word origin = base + start;
    const Word aligned = origin & ~Word(bytes - 1);
    const unsigned leading = unsigned(origin - aligned);
    const unsigned first = !packed ? start + i : i == 0 ? start : start + i * bytes - leading;
    const unsigned end = !packed ? first + 1 : std::min(vl, start + (i + 1) * bytes - leading);
    const Word address = packed ? aligned + i * bytes : base + first * stride;
    eval();
    CHECK(sequence_request_out.pvalid);
    const auto &c = sequence_request_out.pbits.pcontext;
    const auto &b = c.pbeat_ucontrol;
    CHECK(c.ppacked == packed && b.psequence == i && b.paddress == address);
    CHECK(b.pfirst == first && b.pend == end && b.plast == (end == vl));
    Feedback f{};
    f.pcontext = base & ~Word(3);
    f.psequence = i;
    f.pfirst = first;
    f.pend = end;
    f.plast = b.plast;
    if (packed) {
      f.ppacked_ulayout = c.ppacked_ulayout;
      CHECK(f.ppacked_ulayout.pbefore.paddress == address);
      CHECK(f.ppacked_ulayout.pafter.paddress == address + bytes);
    } else {
      f.ppacked_ulayout.pbefore.paddress = ~address; // Not the ordinary checkpoint.
    }
    return f;
  }
};

void run_chain() {
  const Macro chain[] = {
      {false, true, Word(0x100000123ULL), start + 1},
      {true, false, Word(0x200001001ULL), start + 4 * bytes},
      {false, false, Word(0x300002005ULL), start + 6},
      {true, true, Word(0x400003001ULL), start + 1},
      {false, true, Word(0x500004007ULL), start + 1},
  };
  reset = 1;
  tick_model();
  reset = 0;
  chain[0].offer();
  eval();
  CHECK(request_out.pready);
  tick_model();

  for (unsigned m = 0; m < std::size(chain); ++m) {
    const auto &op = chain[m];
    request_in = {};
    packed_memory = !op.packed; // Only the admitted mode may drive issue.
    sequence_request_in.pready = 0;
    for (unsigned n = 0; !op.packed && n < start; ++n) {
      eval();
      CHECK(!sequence_request_out.pvalid);
      tick_model();
    }
    auto issue = [&](unsigned i) {
      sequence_request_in.pready = 1;
      auto f = op.check_beat(i);
      tick_model();
      sequence_request_in.pready = 0;
      return f;
    };
    auto decide = [&](Feedback f, bool retry) {
      feedback_in.pbits = f;
      feedback_in.pbits.pdisposition = retry ? 1 : 0;
      feedback_in.pvalid = 1;
      eval();
      if (retry) CHECK(read_flush && !sequence_request_out.pvalid && !request_out.pready);
      tick_model();
      feedback_in = {};
    };

    unsigned i = 0;
    if (!op.certified) {
      decide(issue(0), false);
      auto rejected = issue(1);
      issue(2);
      op.check_beat(3); // Younger offer stalled behind two outstanding beats.
      tick_model();
      decide(rejected, true);
      for (unsigned n = 0; n < 3; ++n) {
        op.check_beat(1); // Replayed address stays stable until accepted.
        tick_model();
      }
      i = 1;
    }
    while (!op.check_beat(i).plast) decide(issue(i++), false);

    // Accept the successor on the tail issue (certified) or tail authorization.
    if (!op.certified) {
      feedback_in.pbits = issue(i);
      feedback_in.pvalid = 1;
    }
    if (m + 1 < std::size(chain)) chain[m + 1].offer();
    sequence_request_in.pready = 1;
    if (op.certified) op.check_beat(i);
    eval();
    CHECK(request_out.pready && sequencing_done_out.pvalid);
    tick_model();
    feedback_in = {};
  }
  eval();
  CHECK(!active);
}

void reject_wrong_sequence() {
  const Macro op{false, false, 0x240, start + 5};
  op.offer();
  eval();
  CHECK(request_out.pready);
  tick_model();
  request_in = {};
  sequence_request_in.pready = 1;
  for (unsigned n = 0; n < start; ++n) tick_model();

  auto prefix = op.check_beat(0);
  tick_model();
  feedback_in.pvalid = 1;
  feedback_in.pbits = prefix;
  auto oldest = op.check_beat(1);
  tick_model(); // Authorize beat 0 while issuing beat 1.
  feedback_in = {};
  auto younger = op.check_beat(2);
  tick_model();

  // All metadata describes the real oldest beat, except for its sequence.
  sequence_request_in.pready = 0;
  feedback_in.pvalid = 1;
  feedback_in.pbits = oldest;
  feedback_in.pbits.pdisposition = 1; // Retry
  feedback_in.pbits.psequence = younger.psequence;
  expect_failure("vector_feedback_is_oldest_unauthorized_operation", [] { tick_model(); });
}

int main() {
  return run_test([] {
    run_chain();
    reject_wrong_sequence();
  });
}
