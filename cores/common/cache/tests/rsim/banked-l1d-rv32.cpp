// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

template<class F> void until(F done) {
  for (unsigned n = 0; n < 100; ++n) {
    eval();
    if (done()) return;
    tick_model();
  }
  CHECK(false && "RV32 banked L1D timeout");
}

int main() {
  return run_test([] {
    reset = 1; tick_model(); reset = 0;
    node_id = 3; chi_in.prequester_uresponses.pready = 1;
    constexpr unsigned base = 0x1000;
    // Each incoming CHI packet has two 64-bit rows on RV32. Read the
    // entire line back, including banks reused in the second half.
    for (unsigned row = 0; row < 8; ++row) {
      auto &req = core_in.prequest;
      req.pbits = {}; req.pbits.paccess = 1; req.pbits.praw_ubeat = 1;
      req.pbits.pbyte_umask = 0xff; req.pbits.paddress = base + 8*row;
      req.pbits.pcontext = row; req.pvalid = 1;
      virtual_lookup_in.pvalid = 1; virtual_lookup_in.pbits = req.pbits.paddress;
      until([] { return core_out.prequest.pready; });
      tick_model(); req.pvalid = 0; virtual_lookup_in.pvalid = 0;
      if (!row) {
        until([] { return chi_out.prequests.pvalid; });
        const auto txn = chi_out.prequests.pbits.ptxn_uid;
        CHECK(chi_out.prequests.pbits.popcode == 2 && chi_out.prequests.pbits.paddress == base);
        chi_in.prequests.pready = 1; tick_model(); chi_in.prequests.pready = 0;
        for (unsigned i : {2u, 3u, 1u, 0u}) {
          auto &dat = chi_in.presponse_udata;
          dat.pbits = {}; dat.pbits.popcode = 4; dat.pbits.presp = 1;
          dat.pbits.pdata_uid = i; dat.pbits.ptxn_uid = txn;
          dat.pbits.psrc_uid = 1; dat.pbits.ptgt_uid = 3;
          dat.pbits.phome_unid_uor_upbha_uor_umismatched_umecid = 1;
          dat.pbits.pbyte_uenable = 0xffff;
          for (unsigned w = 0; w < 4; ++w) dat.pbits.pdata.words[w] = 1 + 4*i + w;
          dat.pvalid = 1; until([] { return chi_out.presponse_udata.pready; });
          tick_model(); dat.pvalid = 0;
        }
      }
      until([] { return core_out.presponse.pvalid; });
      const auto &rsp = core_out.presponse.pbits;
      CHECK(rsp.pcontext == row && !rsp.paccess_ufault);
      CHECK(rsp.pdata == ((std::uint64_t(2*row + 2) << 32) | (2*row + 1)));
      core_in.presponse.pready = 1; tick_model(); core_in.presponse.pready = 0;
    }
  });
}
