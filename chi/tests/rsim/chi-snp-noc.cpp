// Simulates NodeID-selected CHI SNP delivery through an independently routed
// plane.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void send_snoop(std::uint8_t target_id, bool target_lane,
                std::uint16_t txn_id) {
  int cycles;
  {
    dispatch_in = {};
    dispatch_in.pbits.ptarget_uid = target_id;
    dispatch_in.pbits.pflit.psrc_uid = UINT64_C(5);
    dispatch_in.pbits.pflit.ptxn_uid = txn_id;
    dispatch_in.pbits.pflit.popcode = UINT64_C(10);
    dispatch_in.pbits.pflit.paddress = UINT64_C(74565);
    dispatch_in.pvalid = UINT64_C(1);
    eval();
    while (!dispatch_out.pready)
      tick_model();
    tick_model();
    dispatch_in = {};

    for (cycles = 0; cycles < 8; cycles = cycles + 1) {
      eval();
      if ((target_lane ? snoops_1_out.pvalid : snoops_0_out.pvalid))
        break;
      tick_model();
    }
    CHECK(cycles < 8);
    CHECK(!(target_lane ? snoops_0_out.pvalid : snoops_1_out.pvalid));
    if (target_lane) {
      CHECK(snoops_1_out.pbits.psrc_uid == UINT64_C(5) &&
            snoops_1_out.pbits.ptxn_uid == txn_id &&
            snoops_1_out.pbits.popcode == UINT64_C(10) &&
            snoops_1_out.pbits.paddress == UINT64_C(74565));
    } else {
      CHECK(snoops_0_out.pbits.psrc_uid == UINT64_C(5) &&
            snoops_0_out.pbits.ptxn_uid == txn_id &&
            snoops_0_out.pbits.popcode == UINT64_C(10) &&
            snoops_0_out.pbits.paddress == UINT64_C(74565));
    }
    tick_model();
  }
}

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    dispatch_in = {};
    snoops_0_in.pready = UINT64_C(1);
    snoops_1_in.pready = UINT64_C(1);
    tick_model();
    reset = UINT64_C(0);

    send_snoop(UINT64_C(2), UINT64_C(0), UINT64_C(291));
    send_snoop(UINT64_C(3), UINT64_C(1), UINT64_C(1110));
  });
}
