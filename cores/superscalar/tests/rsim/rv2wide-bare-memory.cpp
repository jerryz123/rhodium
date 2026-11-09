// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void check_split_load(unsigned width, unsigned offset, bool sign_extend) {
  constexpr std::uint64_t base = 0x10000;
  constexpr std::uint8_t bytes[] = {0xf0, 0x71, 0xe2, 0x53, 0xb4, 0x35, 0x96, 0x17,
                                    0x88, 0x19, 0xaa, 0x3b, 0xcc, 0x5d, 0xee, 0x7f};
  const unsigned size = 1u << width;
  address = base + offset;
  access = 1;
  split_width = width;
  split_signed = sign_extend;
  split_valid = 1;
  eval(); CHECK(split_ready);
  tick_model();
  split_valid = 0;
  address = base + 7; // No longer the owner.
  eval();

  const unsigned fragments = offset + size > 8 ? 2 : 1;
  for (unsigned i = 0; i < fragments; ++i) {
    for (unsigned wait = 0; !physical_valid && wait < 20; ++wait) tick_model();
    for (unsigned stall = 0; stall < 3; ++stall) {
      CHECK(physical_valid && physical_address == base + 8*i && !split_completed);
      tick_model(); eval();
    }
    physical_ready = 1;
    tick_model(); physical_ready = 0;
    for (unsigned stall = 0; stall < 3; ++stall) {
      response_data = ~std::uint64_t(stall);
      eval(); CHECK(!physical_valid && !split_completed);
      tick_model();
    }
    std::uint64_t word = 0;
    for (unsigned lane = 0; lane < 8; ++lane)
      word |= std::uint64_t(bytes[8*i + lane]) << (8*lane);
    response_data = word;
    response_valid = 1;
    eval(); CHECK(response_ready && !completed);
    tick_model(); response_valid = 0; eval();
  }

  std::uint64_t expected = 0;
  for (unsigned lane = 0; lane < size; ++lane)
    expected |= std::uint64_t(bytes[offset + lane]) << (8*lane);
  if (sign_extend && size < 8 && (bytes[offset + size - 1] & 0x80))
    expected |= ~UINT64_C(0) << (8*size);
  CHECK(split_completed && split_result == expected);
  CHECK(!split_page_fault && !split_access_fault && !completed);
  tick_model(); eval(); CHECK(memory_drained);
}

int main() {
  return run_test([] {
    lookup_outcome = 1; // Physical LoadHit.
    reset = 1;
    tick_model();
    reset = 0;
    eval();
    CHECK(!vector_fast_out.presponse.pvalid);

    // Bare hints retain one cycle of latency and suppress the outgoing edge on invalidation.
    prefetch_in.pvalid = 1;
    prefetch_in.pbits = {0x10025, 2};
    eval();
    CHECK(!physical_prefetch_out.pvalid);
    tick_model();
    prefetch_in.pvalid = 0;
    eval();
    CHECK(physical_prefetch_out.pvalid && physical_prefetch_out.pbits.paddress == 0x10000);
    invalidate = 1;
    eval();
    CHECK(!physical_prefetch_out.pvalid);
    tick_model();
    invalidate = 0;
    prefetch_in.pvalid = 1;
    prefetch_in.pbits = {0x2000, 2};
    tick_model();
    prefetch_in.pvalid = 0;
    eval();
    CHECK(!physical_prefetch_out.pvalid);
    tick_model();

    // Identity also keeps whole-state cancellation on the outgoing hint edge.
    prefetch_in = {1, {0x10025, 2}};
    tick_model();
    prefetch_in.pvalid = 0;
    eval();
    CHECK(physical_prefetch_out.pvalid);
    mstatus ^= UINT64_C(1) << 3;
    eval();
    CHECK(!physical_prefetch_out.pvalid);
    tick_model();

    // EX indexes immediately; MEM resolves the same owner exactly one edge later.
    address = 0x10018;
    access = 1;
    ex_valid = 1;
    eval();
    CHECK(index_valid && index_address == address && !result_valid);
    tick_model();
    ex_valid = 0;
    eval();
    CHECK(result_valid && resolve_valid && resolve_address == address && outcome == 1);
    // Both physical fault classes belong to this MEM lookup. Only an absent reply replays it.
    for (unsigned fault : {4u, 5u}) {
      lookup_outcome = fault;
      eval();
      CHECK(result_valid && resolve_valid && outcome == fault);
    }
    suppress_lookup_response = 1;
    eval();
    CHECK(result_valid && outcome == 3);
    suppress_lookup_response = 0;
    lookup_outcome = 1;
    eval();
    CHECK(result_valid && outcome == 1);
    tick_model();
    eval();
    CHECK(!result_valid && memory_drained);

    // A rejected offer owns no response; acceptance survives later invalidation.
    wb_valid = 1;
    physical_ready = 0;
    eval();
    CHECK(physical_valid && !wb_ready && memory_drained);
    tick_model();
    physical_fault = 1;
    eval();
    CHECK(wb_fault && !wb_ready && memory_drained);
    tick_model();
    physical_fault = 0;
    physical_ready = 1;
    eval();
    CHECK(wb_ready);
    tick_model();
    wb_valid = 0;
    physical_ready = 0;
    invalidate = 1;
    tick_model();
    invalidate = 0;
    block_response = 1;
    response_valid = 1;
    response_data = 0x12345678;
    eval();
    CHECK(completed && completed_data == response_data && !response_ready && !memory_drained);
    tick_model();
    block_response = 0;
    eval();
    CHECK(response_ready && completed && completed_data == response_data);
    tick_model();
    response_valid = 0;
    eval();
    CHECK(memory_drained && !completed);

    for (unsigned width = 1; width <= 3; ++width)
      for (unsigned offset = 1; offset < 8; ++offset)
        if (offset % (1u << width) != 0)
          for (bool sign_extend : {false, true})
            check_split_load(width, offset, sign_extend);

    for (bool store : {false, true}) {
      vector_fast_in.prequest = {1, {0x10008, 0x100ff, store}};
      eval();
      CHECK(vector_fast_out.presponse.pvalid);
      auto proof = vector_fast_out.presponse.pbits;
      CHECK(proof.pppn == 0x10 && !proof.pvirtualized);
      CHECK(proof.prange.pfirst == 0x10008 && proof.prange.plast == 0x100ff);
      CHECK(proof.prange.pstore == store);
      vector_fast_in.pcandidate = proof;
      // Bare validation ignores context fields; invalidation alone changes epochs.
      vector_fast_in.pcandidate.pvirtualized = 1;
      vector_fast_in.pcandidate.pcontext.pvs_uroot_uppn = 0xabc;
      eval();
      CHECK(vector_fast_out.pcandidate_ucurrent);
      invalidate = 1;
      eval();
      CHECK(!vector_fast_out.presponse.pvalid && vector_fast_out.pcandidate_ucurrent);
      tick_model();
      eval();
      CHECK(!vector_fast_out.pcandidate_ucurrent);
      invalidate = 0;
      eval();
      CHECK(vector_fast_out.presponse.pvalid);
    }

    for (auto first : {UINT64_C(0x100000000010008), UINT64_C(0x3000), UINT64_C(0x2000), UINT64_C(0x2300)}) {
      vector_fast_in.prequest = {1, {first, first + 7, 0}};
      eval();
      CHECK(!vector_fast_out.presponse.pvalid && !physical_valid);
    }
    vector_fast_in.prequest.pvalid = 0;
    eval();
    CHECK(!vector_fast_out.presponse.pvalid);
  });
}
