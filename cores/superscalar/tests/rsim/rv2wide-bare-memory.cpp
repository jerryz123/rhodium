// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
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

    // Split fragments retain their separate response route and exact byte lanes.
    address = 0x10003;
    split_width = 3;
    split_valid = 1;
    eval();
    CHECK(split_ready);
    tick_model();
    split_valid = 0;
    for (unsigned fragment = 0; fragment < 2; ++fragment) {
      for (unsigned wait = 0; !physical_valid && wait < 20; ++wait) tick_model();
      CHECK(physical_valid && physical_address == 0x10000 + fragment * 8);
      physical_ready = 1;
      tick_model();
      physical_ready = 0;
      response_data = fragment ? UINT64_C(0x0f0e0d0c0b0a0908) : UINT64_C(0x0706050403020100);
      response_valid = 1;
      eval();
      CHECK(response_ready && !completed);
      tick_model();
      response_valid = 0;
      eval();
    }
    for (unsigned wait = 0; !split_completed && wait < 20; ++wait) tick_model();
    CHECK(split_completed && split_result == UINT64_C(0x0a09080706050403));
    CHECK(!split_page_fault && !split_access_fault && !completed);
    tick_model();
    eval();
    CHECK(memory_drained);

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
