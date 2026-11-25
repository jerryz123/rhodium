// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

using Word = decltype(prepare_results_1_in.pbits);
constexpr unsigned vlen = 128, row_bytes = sizeof(Word), rows = vlen / (8 * row_bytes);

void gather(bool ei16, unsigned sew, unsigned lmul, Word index, bool stall, Word type_flags = 0) {
  constexpr unsigned start = 1, vs1 = 16, vs2 = 8;
  const unsigned bytes = 1u << sew, index_bytes = ei16 ? 2 : bytes;
  const unsigned vlmax = ((vlen / 8) << lmul) / bytes;
  const bool in_range = !(type_flags >> 8) && lmul != 4 && index < vlmax;
  request_in = {};
  auto &d = request_in.pbits.pdescriptor;
  d.pinstruction = (vs2 << 20) | (vs1 << 15);
  d.pcontrol.pinteger = 1;
  d.pcontrol.pgather = 1;
  d.pcontrol.pgather_uindex16 = ei16;
  d.pstate.pvtype = type_flags | (sew << 3) | lmul;
  d.pstate.pvstart = start;
  d.pstate.pvl = start + 1;
  request_in.pvalid = 1;
  sequence_request_in.pready = 1;
  eval();
  CHECK(request_out.pready);
  tick_model();
  request_in = {};
  eval();
  CHECK(prepare_reads_1_out.pvalid && !sequence_request_out.pvalid);
  CHECK(prepare_reads_1_out.pbits == vs1 * rows + start * index_bytes / row_bytes);
  tick_model();

  // Select a nonzero lane where possible; all neighboring indices are out of range.
  const unsigned shift = 8 * ((start * index_bytes) % row_bytes);
  const Word mask = Word(low_mask(8 * index_bytes));
  prepare_results_1_in.pbits = (~(mask << shift)) | (index << shift);
  prepare_results_1_in.pvalid = 1;
  sequence_request_in.pready = !stall;
  auto check_offer = [&] {
    eval();
    CHECK(sequence_request_out.pvalid && !prepare_reads_1_out.pvalid);
    const auto &offer = sequence_request_out.pbits;
    CHECK(offer.pcontext.pbeat_ucontrol.pfirst == start && offer.pcontext.pbeat_ucontrol.plast);
    CHECK(offer.pcontext.pgather_uin_urange == in_range);
    CHECK((offer.penables & 1) && offer.paddresses[0] == vs2 * rows + (in_range ? index * bytes / row_bytes : 0));
    if (in_range)
      CHECK(offer.pcontext.pgather_uoffset == (index * bytes) % row_bytes);
  };

  check_offer(); // Valid on the VRF return cycle, even when ready is already high.
  tick_model();
  prepare_results_1_in.pvalid = 0;
  if (stall) {
    for (unsigned cycle = 0; cycle < 4; ++cycle) {
      prepare_results_1_in.pbits = (cycle & 1) ? ~Word(0) : Word(0);
      sequence_request_in.pready = cycle == 3;
      check_offer();
      tick_model();
    }
  }
  eval();
  CHECK(!active && !sequence_request_out.pvalid);
}

int main() {
  return run_test([] {
    reset = 1;
    tick_model();
    reset = 0;
    constexpr unsigned native_sew = row_bytes == 8 ? 3 : 2;
    for (bool stall : {false, true}) {
      // Same low byte: policy bits do not affect VLMAX, but every higher bit does.
      constexpr Word policy = 3u << 6;
      gather(false, 0, 0, 5, stall, policy);
      for (unsigned bit = 8; bit < 8 * row_bytes; ++bit)
        gather(false, 0, 0, 5, stall, policy | (Word(1) << bit));
      gather(false, 0, 4, 0, stall, policy); // Reserved LMUL also gives VLMAX=0.
      gather(false, 0, 3, 127, stall); // Last valid index at maximum VLMAX.
      gather(false, native_sew, 3, 5, stall);
      gather(false, native_sew, 3, (Word(1) << (8 * row_bytes - 1)) | 5, stall);
      gather(true, 0, 1, 11, stall); // EI16 uses 16-bit indices even for byte data.
      gather(true, 0, 1, 0x800b, stall);
      gather(true, 0, 1, 32, stall); // Equal to this descriptor's VLMAX.
    }
  });
}
