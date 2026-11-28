// Checks that the DPI memory returns an unstalled 64-byte read in four consecutive beats.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

extern "C" char rhodium_chi_memory_init(int, long long, long long) { return 0; }

extern "C" char rhodium_chi_memory_access(int, long long, char beat_bytes,
                                          unsigned char write, long long address,
                                          const std::uint32_t*, long long, std::uint32_t* data) {
  CHECK(!write && beat_bytes == 16 && address >= 0 && address < 64 && address % 16 == 0);
  std::fill_n(data, 16, 0);
  data[0] = static_cast<std::uint32_t>(address);
  return 0;
}

int main() {
  return run_test([] {
    identity.pnode_uid = 9;
    identity.pbase_uaddress = 0x80000000;
    reset = 1;
    tick_model();
    reset = 0;

    port_in.pdat.presponse.pready = 1;
    auto& req = port_in.preq;
    req.pvalid = 1;
    req.pbits.popcode = 4; // ReadNoSnp
    req.pbits.psrc_uid = 3;
    req.pbits.ptgt_uid = 9;
    req.pbits.ptxn_uid = 5;
    req.pbits.paddress = 0x80000000;
    req.pbits.psize_uor_unum_ureq = 6; // 64 bytes
    req.pbits.preturn_unid_uor_ustash_unid_uor_udata_utarget = 3;
    req.pbits.preturn_utxn_uid_uor_ustash_ulpid = 5;
    eval();
    CHECK(port_out.preq.pready);
    tick_model();
    req.pvalid = 0;
    eval();

    const auto& dat = port_out.pdat.presponse;
    for (unsigned wait = 0; !dat.pvalid && wait < 20; ++wait) tick_model();
    for (unsigned beat = 0; beat < 4; ++beat) {
      require(dat.pvalid, "bubble in unstalled DAT response");
      CHECK(dat.pbits.popcode == 4 && dat.pbits.ptxn_uid == 5);
      CHECK(dat.pbits.pdata_uid == beat && dat.pbits.pdata.words[0] == beat * 16);
      tick_model();
    }
    CHECK(!dat.pvalid);
  });
}
