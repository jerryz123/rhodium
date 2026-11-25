// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <initializer_list>

using Word = decltype(response_in.pbits.pdata);
constexpr unsigned unmasked = 1u << 25, e32 = 6u << 12;
constexpr unsigned load = unmasked | e32 | 0x07, store = unmasked | e32 | 0x27;
constexpr unsigned move = (23u << 26) | unmasked | (6u << 15) | 0x57;

static void step() {
  CHECK(test_cycles < 300);
  tick_model();
  eval();
}

static void offer(unsigned insn, Word integer, Word fp, bool packed, unsigned length) {
  instruction = insn;
  scalar = integer;
  fp_scalar = fp;
  packed_memory = packed;
  vl = length;
  request_valid = 1;
  eval();
}

static void accept() {
  while (!request_ready) step();
  step();
  request_valid = 0;
  eval();
}

static void pending(unsigned address, bool packed) {
  while (!resolution_out.pvalid) step();
  CHECK(resolution_out.pbits.paddress == address && resolution_out.pbits.ppacked == packed);
  CHECK(resolution_out.pbits.penabled);
}

static void decide() {
  decision_hold = 0;
  eval();
  step();
  decision_hold = 1;
  eval();
}

static void handoff() {
  CHECK(resolution_out.pvalid && resolution_out.pbits.plast && !request_ready);
  decision_hold = 0;
  eval();
  CHECK(sequencing_finished && request_ready);
  accept(); // The old memory tail and the new descriptor transfer on the same edge.
  decision_hold = 1;
  eval();
}

static Word mask_to_fused(bool old_vd_is_product, Word old_vd) {
  // vmand.mm v19,v9,v8 immediately precedes vfmacc.vv or vfmadd.vv v18,v9,v8.
  offer((25u << 26) | unmasked | (9u << 20) | (8u << 15) | (2u << 12) | (19u << 7) | 0x57, 0, 0, false, 1);
  accept();
  offer(((old_vd_is_product ? 40u : 44u) << 26) | unmasked | (8u << 20) | (9u << 15) |
        (1u << 12) | (18u << 7) | 0x57, 0, 0, false, 1);
  accept();
  while (!fp_request_out.pvalid) step();
  const auto tag = fp_request_out.pbits.ptag;
  const auto boxed = [](Word word) { return ~Word(UINT32_MAX) | word; };
  for (unsigned i = 0; i < 3; ++i) {
    const auto& req = fp_request_out.pbits;
    CHECK(fp_request_out.pvalid && !fp_request_in.pready && req.ptag == tag);
    CHECK(req.pleft == boxed(old_vd_is_product ? old_vd : 0x11112222));
    CHECK(req.pright == boxed(0x33334444));
    CHECK(req.pthird == boxed(old_vd_is_product ? 0x11112222 : old_vd));
    step();
  }
  fp_request_in.pready = 1;
  step();
  fp_request_in.pready = 0;
  const Word writeback = old_vd + 1;
  fp_result_in.pbits.ptag = tag;
  fp_result_in.pbits.pvalue = boxed(writeback);
  fp_result_in.pvalid = 1;
  eval();
  while (!fp_result_out.pready) step();
  step();
  fp_result_in.pvalid = 0;
  while (active) step();
  return writeback;
}

int main() {
  return run_test([] {
    reset = issue_ready = decision_hold = 1;
    step();
    reset = 0;
    vtype = 2 << 3; // e32,m1
    offer(load | (8u << 7), 0x100, 0xaaaa, true, 1);
    accept();
    offer(load | (2u << 26) | (3u << 20) | (9u << 7), 0x240, 0xbbbb, false, 2);
    pending(0x100, true);
    hit_data = 0x11112222;
    handoff(); // packed base -> ordinary base and stride (8, not EEW)

    constexpr Word gpr = 0x12345, bad_box = 0x3f800000;
    offer(move | (4u << 12) | (16u << 7), gpr, 0xcccc, false, 1);
    pending(0x240, false);
    hit_data = 0x33334444;
    decide();
    pending(0x248, false);
    hit_data = 0x55556666;
    handoff(); // stalled strided tail -> integer operand
    offer(move | (5u << 12) | (17u << 7), 0xdddd, bad_box, false, 1);
    accept(); // FP move with a different source on the very next offer

    auto readback = [&](unsigned vd, std::initializer_list<unsigned> words) {
      offer(store | (vd << 7), 0x380, 0xeeee, false, words.size());
      accept();
      unsigned address = 0x380;
      for (auto word : words) {
        pending(address, false);
        CHECK(resolution_out.pbits.pstore);
        if (std::uint32_t(resolution_out.pbits.pstore_udata) != word)
          throw std::runtime_error("v" + std::to_string(vd) + " readback mismatch: expected " + std::to_string(word) +
                                   ", got " + std::to_string(std::uint32_t(resolution_out.pbits.pstore_udata)));
        decide();
        address += 4;
      }
    };
    readback(8, {0x11112222});
    readback(9, {0x33334444, 0x55556666});
    readback(16, {gpr});
    constexpr unsigned fp_word = sizeof(Word) == 8 ? 0x7fc00000u : unsigned(bad_box);
    readback(17, {fp_word});
    while (active) step();

    // A two-field store uses prepared v16/v17 data, not the direct-store bypass.
    // Hold a different scalar move at admission while the store cannot issue.
    issue_ready = 0;
    offer(store | (1u << 29) | (16u << 7), 0x500, 0xbadd, true, 1);
    accept();
    constexpr unsigned next_scalar = 0x2468ace;
    offer(move | (4u << 12) | (18u << 7), next_scalar, 0xbeef, false, 1);
    for (unsigned i = 0; i < 5; ++i) {
      CHECK(!issued && !request_ready);
      step();
    }
    issue_ready = 1;
    eval();
    pending(0x500, true);
    CHECK(resolution_out.pbits.pstore && std::uint32_t(resolution_out.pbits.pstore_udata) == gpr);
    if constexpr (sizeof(Word) == 8) {
      CHECK((std::uint64_t(resolution_out.pbits.pstore_udata) >> 32) == fp_word);
    } else {
      decide();
      pending(0x504, true);
      CHECK(resolution_out.pbits.pstore && std::uint32_t(resolution_out.pbits.pstore_udata) == fp_word);
    }
    handoff();
    readback(18, {next_scalar});
    while (active) step();
    const Word acc = mask_to_fused(false, next_scalar);
    const Word product = mask_to_fused(true, acc);
    readback(18, {static_cast<unsigned>(product)});
  });
}
