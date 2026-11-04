// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = 1;
    tick_model();
    reset = 0;
    eval();
    CHECK(!vector_fast_out.presponse.pvalid);

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
