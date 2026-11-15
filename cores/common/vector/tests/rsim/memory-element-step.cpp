// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

using Word = decltype(request_in.pbits.pdescriptor.pscalar);
enum class Addressing { UnitStride, Strided };

void check_access(Addressing addressing, unsigned nf, unsigned eew, Word stride,
                  bool store, bool whole = false) {
  constexpr Word base = 0x200;
  constexpr unsigned start = 1;
  const unsigned bytes = 1u << eew;
  const unsigned fields = whole ? 1 : nf + 1;
  const unsigned limit = whole ? (16 * (nf + 1)) / bytes : 6; // VLEN=128
  const unsigned beats = (limit - start) * fields;
  const Word step = addressing == Addressing::Strided ? stride : bytes * fields;

  reset = 1;
  feedback_in = {};
  sequence_request_in = {};
  request_in = {};
  tick_model();
  reset = 0;
  auto &d = request_in.pbits.pdescriptor;
  d.ppc = 0x24;
  d.pscalar = base;
  d.pstride = stride;
  d.pstate.pvstart = start;
  d.pstate.pvl = 6;
  d.pstate.pvtype = eew << 3;
  d.pcontrol.pmemory = 1;
  d.pcontrol.pstore = store;
  d.pcontrol.pmemory_uaddressing = static_cast<unsigned>(addressing);
  d.pcontrol.pmemory_umode = whole ? 2 : 0;
  d.pcontrol.pmemory_udata_ueew = eew;
  d.pcontrol.pmemory_unf = nf;
  request_in.pvalid = 1;
  eval();
  CHECK(request_out.pready);
  tick_model();
  request_in = {}; // Neither cursor may consult the next request.

  auto issue = [&](unsigned i) {
    const unsigned element = start + i / fields;
    const Word address = base + element * step + (i % fields) * bytes;
    sequence_request_in.pready = 0;
    eval();
    for (unsigned cycle = 0; !sequence_request_out.pvalid && cycle < 10; ++cycle)
      tick_model();
    for (unsigned cycle = 0; cycle < 4; ++cycle) {
      sequence_request_in.pready = cycle == 3;
      eval();
      CHECK(sequence_request_out.pvalid);
      const auto &beat = sequence_request_out.pbits.pcontext.pbeat_ucontrol;
      CHECK(beat.paddress == address && beat.psequence == i);
      CHECK(beat.pfirst == element && beat.pend == element + 1 && beat.plast == (i + 1 == beats));
      tick_model();
    }
    sequence_request_in.pready = 0;
  };

  auto decide = [&](unsigned i, bool retry) {
    feedback_in.pvalid = 1;
    auto &f = feedback_in.pbits;
    f.pcontext = 0x24;
    f.pfirst = start + i / fields;
    f.pend = f.pfirst + 1;
    f.psequence = i;
    f.pdisposition = retry ? 1 : 0;
    f.plast = i + 1 == beats;
    tick_model();
    feedback_in = {};
  };

  // Authorize the prefix, then issue across an element boundary without
  // authorization. Retrying the oldest beat must restore its base AND field.
  const unsigned rejected = fields + (fields > 1);
  for (unsigned i = 0; i < rejected; ++i) {
    issue(i);
    decide(i, false);
  }
  for (unsigned i = rejected; i <= rejected + fields; ++i)
    issue(i);
  decide(rejected, true);
  for (unsigned i = rejected; i < beats; ++i) {
    issue(i);
    decide(i, false);
  }
  eval();
  CHECK(!active);
}

int main() {
  return run_test([] {
    check_access(Addressing::UnitStride, 2, 1, 0x55, false);
    check_access(Addressing::UnitStride, 7, 0, 0x55, true);
    check_access(Addressing::UnitStride, 3, 2, 0x55, true, true);
    check_access(Addressing::Strided, 2, 1, Word(-16), false);
    check_access(Addressing::Strided, 1, 0, 0, true);
    check_access(Addressing::Strided, 0, 1, Word(0x100000018ULL), false);
  });
}
