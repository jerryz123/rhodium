// Simulates legal initial-profile CHI transactions at a Subordinate Node.
// SPDX-License-Identifier: Apache-2.0
#include "request.hpp"
#include "test.hpp"
#include "wide.hpp"
using CHIReqFlit = std::remove_cvref_t<decltype(port_in.prx_ureq.pbits)>;
using CHIRspFlit = std::remove_cvref_t<decltype(tx_rsp_bits)>;
using CHIDatFlit = std::remove_cvref_t<decltype(tx_dat_bits)>;
using dat_t = std::remove_cvref_t<decltype(port_in.prx_udat)>;
using rsp_t = std::remove_cvref_t<decltype(port_out.ptx_ursp)>;
using req_t = std::remove_cvref_t<decltype(port_in.prx_ureq)>;
bool tx_link_active_ack{};
bool rx_link_active_request{};

void clear_flits() {
  {
    tx_rsp_valid = UINT64_C(0);
    tx_rsp_bits = {};
    tx_dat_valid = UINT64_C(0);
    tx_dat_bits = {};
    port_in.prx_ureq.pvalid = UINT64_C(0);
    port_in.prx_ureq.pbits = {};
    port_in.prx_udat.pvalid = UINT64_C(0);
    port_in.prx_udat.pbits = {};
  }
}

void run_case() {

  reset = UINT64_C(1);
  tx_link_active_request = UINT64_C(0);
  rx_link_active_ack = UINT64_C(0);
  rx_req_credit = UINT64_C(0);
  rx_dat_credit = UINT64_C(0);
  port_in = {};
  clear_flits();
  tick_model();

  reset = UINT64_C(0);
  tx_link_active_request = UINT64_C(1);
  port_in.prx_ulink_uactive_urequest = UINT64_C(1);
  tick_model();
  port_in.ptx_ulink_uactive_uack = UINT64_C(1);
  rx_link_active_ack = UINT64_C(1);
  tick_model();

  rx_req_credit = UINT64_C(1);
  port_in.ptx_udat.pcredit = UINT64_C(1);
  tick_model();
  rx_req_credit = UINT64_C(0);
  port_in.ptx_udat.pcredit = UINT64_C(0);
  port_in.prx_ureq.pvalid = UINT64_C(1);
  port_in.prx_ureq.pbits.popcode = UINT64_C(4);
  port_in.prx_ureq.pbits.psrc_uid = UINT64_C(7);
  port_in.prx_ureq.pbits.ptgt_uid = UINT64_C(9);
  port_in.prx_ureq.pbits.ptxn_uid = UINT64_C(769);
  port_in.prx_ureq.pbits.preturn_unid_uor_ustash_unid_uor_udata_utarget =
      UINT64_C(3);
  port_in.prx_ureq.pbits.preturn_utxn_uid_uor_ustash_ulpid = UINT64_C(257);
  port_in.prx_ureq.pbits.psize_uor_unum_ureq = UINT64_C(4);
  tick_model();
  clear_flits();

  tx_dat_valid = UINT64_C(1);
  tx_dat_bits.popcode = UINT64_C(4);
  tx_dat_bits.psrc_uid = UINT64_C(9);
  tx_dat_bits.ptgt_uid = UINT64_C(3);
  tx_dat_bits.ptxn_uid = UINT64_C(257);
  tick_model();
  clear_flits();

  rx_req_credit = UINT64_C(1);
  port_in.ptx_ursp.pcredit = UINT64_C(1);
  tick_model();
  rx_req_credit = UINT64_C(0);
  port_in.ptx_ursp.pcredit = UINT64_C(0);
  port_in.prx_ureq.pvalid = UINT64_C(1);
  port_in.prx_ureq.pbits.popcode = UINT64_C(28);
  port_in.prx_ureq.pbits.psrc_uid = UINT64_C(7);
  port_in.prx_ureq.pbits.ptgt_uid = UINT64_C(9);
  port_in.prx_ureq.pbits.ptxn_uid = UINT64_C(770);
  port_in.prx_ureq.pbits.psize_uor_unum_ureq = UINT64_C(4);
  tick_model();
  clear_flits();

  tx_rsp_valid = UINT64_C(1);
  tx_rsp_bits.popcode = UINT64_C(6);
  tx_rsp_bits.psrc_uid = UINT64_C(9);
  tx_rsp_bits.ptgt_uid = UINT64_C(7);
  tx_rsp_bits.ptxn_uid = UINT64_C(770);
  tx_rsp_bits.pdbid_uor_ugroup_uid = UINT64_C(68);
  tick_model();
  clear_flits();

  rx_dat_credit = UINT64_C(1);
  port_in.ptx_ursp.pcredit = UINT64_C(1);
  tick_model();
  rx_dat_credit = UINT64_C(0);
  port_in.ptx_ursp.pcredit = UINT64_C(0);
  port_in.prx_udat.pvalid = UINT64_C(1);
  port_in.prx_udat.pbits.popcode = UINT64_C(3);
  port_in.prx_udat.pbits.psrc_uid = UINT64_C(7);
  port_in.prx_udat.pbits.ptgt_uid = UINT64_C(9);
  port_in.prx_udat.pbits.ptxn_uid = UINT64_C(68);
  port_in.prx_udat.pbits.pbyte_uenable = UINT64_C(255);
  tick_model();
  clear_flits();

  tx_rsp_valid = UINT64_C(1);
  tx_rsp_bits.popcode = UINT64_C(4);
  tx_rsp_bits.psrc_uid = UINT64_C(9);
  tx_rsp_bits.ptgt_uid = UINT64_C(7);
  tx_rsp_bits.ptxn_uid = UINT64_C(770);
  tx_rsp_bits.pdbid_uor_ugroup_uid = UINT64_C(68);
  tick_model();
  clear_flits();

  tx_link_active_request = UINT64_C(0);
  port_in.prx_ulink_uactive_urequest = UINT64_C(0);
  tick_model();
  port_in.ptx_ulink_uactive_uack = UINT64_C(0);
  rx_link_active_ack = UINT64_C(0);
  tick_model();
}

int main() { return run_test(run_case); }
