// Checks coherent reads, copyback grants and packets, and forwarded snoop
// completion.
// SPDX-License-Identifier: Apache-2.0
#include "request.hpp"
#include "test.hpp"
#include "wide.hpp"
using CHIReqFlit = std::remove_cvref_t<decltype(tx_req_bits)>;
using CHIRspFlit = std::remove_cvref_t<decltype(tx_rsp_bits)>;
using CHIDatFlit = std::remove_cvref_t<decltype(tx_dat_bits)>;
using dat_t = std::remove_cvref_t<decltype(port_in.prx_udat)>;
using rsp_t = std::remove_cvref_t<decltype(port_in.prx_ursp)>;
using req_t = std::remove_cvref_t<decltype(port_out.ptx_ureq)>;
bool tx_link_active_ack{};
bool rx_link_active_request{};
int EARLY_COPYBACK = 0;

void clear_flits() {
  {
    tx_req_valid = UINT64_C(0);
    tx_req_bits = {};
    tx_rsp_valid = UINT64_C(0);
    tx_rsp_bits = {};
    tx_dat_valid = UINT64_C(0);
    tx_dat_bits = {};
    port_in.prx_ursp.pvalid = UINT64_C(0);
    port_in.prx_ursp.pbits = {};
    port_in.prx_udat.pvalid = UINT64_C(0);
    port_in.prx_udat.pbits = {};
    port_in.prx_usnp.pvalid = UINT64_C(0);
    port_in.prx_usnp.pbits = {};
  }
}

void run_case() {

  reset = UINT64_C(1);
  tx_link_active_request = UINT64_C(0);
  rx_link_active_ack = UINT64_C(0);
  rx_rsp_credit = UINT64_C(0);
  rx_dat_credit = UINT64_C(0);
  rx_snp_credit = UINT64_C(0);
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

  port_in.ptx_ureq.pcredit = UINT64_C(1);
  rx_dat_credit = UINT64_C(1);
  tick_model();
  port_in.ptx_ureq.pcredit = UINT64_C(0);
  tick_model();
  rx_dat_credit = UINT64_C(0);
  tx_req_valid = UINT64_C(1);
  tx_req_bits.popcode = UINT64_C(1);
  tx_req_bits.psrc_uid = UINT64_C(5);
  tx_req_bits.ptgt_uid = UINT64_C(9);
  tx_req_bits.ptxn_uid = UINT64_C(257);
  tx_req_bits.psize_uor_unum_ureq = UINT64_C(5);
  tx_req_bits.pexp_ucomp_uack = UINT64_C(1);
  tx_req_bits.pallow_uretry = UINT64_C(1);
  tick_model();
  clear_flits();

  port_in.prx_udat.pvalid = UINT64_C(1);
  port_in.prx_udat.pbits.popcode = UINT64_C(4);
  port_in.prx_udat.pbits.psrc_uid = UINT64_C(9);
  port_in.prx_udat.pbits.ptgt_uid = UINT64_C(5);
  port_in.prx_udat.pbits.ptxn_uid = UINT64_C(257);
  port_in.prx_udat.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
      UINT64_C(9);
  port_in.prx_udat.pbits.pdbid_uor_umecid = UINT64_C(85);
  port_in.prx_udat.pbits.pdata_uid = UINT64_C(1);
  tick_model();
  clear_flits();

  port_in.ptx_ursp.pcredit = UINT64_C(1);
  tick_model();
  port_in.ptx_ursp.pcredit = UINT64_C(0);
  tx_rsp_valid = UINT64_C(1);
  tx_rsp_bits.popcode = UINT64_C(2);
  tx_rsp_bits.psrc_uid = UINT64_C(5);
  tx_rsp_bits.ptgt_uid = UINT64_C(9);
  tx_rsp_bits.ptxn_uid = UINT64_C(85);
  tick_model();
  clear_flits();

  port_in.prx_udat.pvalid = UINT64_C(1);
  port_in.prx_udat.pbits.popcode = UINT64_C(4);
  port_in.prx_udat.pbits.psrc_uid = UINT64_C(9);
  port_in.prx_udat.pbits.ptgt_uid = UINT64_C(5);
  port_in.prx_udat.pbits.ptxn_uid = UINT64_C(257);
  port_in.prx_udat.pbits.phome_unid_uor_upbha_uor_umismatched_umecid =
      UINT64_C(9);
  port_in.prx_udat.pbits.pdbid_uor_umecid = UINT64_C(85);
  port_in.prx_udat.pbits.pdata_uid = UINT64_C(0);
  tick_model();
  clear_flits();

  rx_snp_credit = UINT64_C(1);
  tick_model();
  rx_snp_credit = UINT64_C(0);
  port_in.prx_usnp.pvalid = UINT64_C(1);
  port_in.prx_usnp.pbits.popcode = UINT64_C(7);
  port_in.prx_usnp.pbits.psrc_uid = UINT64_C(9);
  port_in.prx_usnp.pbits.ptxn_uid = UINT64_C(514);
  tick_model();
  clear_flits();

  port_in.ptx_ursp.pcredit = UINT64_C(1);
  tick_model();
  port_in.ptx_ursp.pcredit = UINT64_C(0);
  tx_rsp_valid = UINT64_C(1);
  tx_rsp_bits.popcode = UINT64_C(1);
  tx_rsp_bits.psrc_uid = UINT64_C(5);
  tx_rsp_bits.ptgt_uid = UINT64_C(9);
  tx_rsp_bits.ptxn_uid = UINT64_C(514);
  tick_model();
  clear_flits();

  rx_snp_credit = UINT64_C(1);
  tick_model();
  rx_snp_credit = UINT64_C(0);
  port_in.prx_usnp.pvalid = UINT64_C(1);
  port_in.prx_usnp.pbits.popcode = UINT64_C(17);
  port_in.prx_usnp.pbits.psrc_uid = UINT64_C(9);
  port_in.prx_usnp.pbits.ptxn_uid = UINT64_C(771);
  port_in.prx_usnp.pbits.pfwd_unid_uor_upbha = UINT64_C(7);
  port_in.prx_usnp.pbits.pfwd_utxn_uid_uor_ustash_ulpid_uor_uvmid_uext =
      UINT64_C(1028);
  tick_model();
  clear_flits();

  port_in.ptx_ursp.pcredit = UINT64_C(1);
  port_in.ptx_udat.pcredit = UINT64_C(1);
  tick_model();
  port_in.ptx_ursp.pcredit = UINT64_C(0);
  port_in.ptx_udat.pcredit = UINT64_C(0);
  tx_rsp_valid = UINT64_C(1);
  tx_rsp_bits.popcode = UINT64_C(9);
  tx_rsp_bits.psrc_uid = UINT64_C(5);
  tx_rsp_bits.ptgt_uid = UINT64_C(9);
  tx_rsp_bits.ptxn_uid = UINT64_C(771);
  tick_model();
  clear_flits();

  tx_dat_valid = UINT64_C(1);
  tx_dat_bits.popcode = UINT64_C(4);
  tx_dat_bits.psrc_uid = UINT64_C(5);
  tx_dat_bits.ptgt_uid = UINT64_C(7);
  tx_dat_bits.ptxn_uid = UINT64_C(1028);
  tick_model();
  clear_flits();

  for (int order = 0; order < 3; order++) {
    port_in.ptx_ureq.pcredit = 1;
    port_in.ptx_ursp.pcredit = 1;
    rx_dat_credit = 1;
    tick_model();
    port_in.ptx_ureq.pcredit = 0;
    port_in.ptx_ursp.pcredit = 0;
    tick_model();
    rx_dat_credit = 0;
    tx_req_valid = 1;
    tx_req_bits.popcode = UINT64_C(1);
    tx_req_bits.psrc_uid = 5;
    tx_req_bits.ptgt_uid = 9;
    tx_req_bits.ptxn_uid = UINT64_C(257);
    tx_req_bits.psize_uor_unum_ureq = 5;
    tx_req_bits.pexp_ucomp_uack = 1;
    tx_req_bits.pallow_uretry = 1;
    tick_model();
    clear_flits();
    port_in.prx_udat.pvalid = 1;
    port_in.prx_udat.pbits.popcode = 4;
    port_in.prx_udat.pbits.psrc_uid = 9;
    port_in.prx_udat.pbits.ptgt_uid = 5;
    port_in.prx_udat.pbits.ptxn_uid = UINT64_C(257);
    port_in.prx_udat.pbits.phome_unid_uor_upbha_uor_umismatched_umecid = 9;
    port_in.prx_udat.pbits.pdbid_uor_umecid =
        UINT64_C(96) + ((order)&low_mask(16));
    port_in.prx_udat.pbits.pdata_uid = 1;
    tick_model();
    port_in.prx_udat.pvalid = 0;
    tx_rsp_bits.popcode = 2;
    tx_rsp_bits.psrc_uid = 5;
    tx_rsp_bits.ptgt_uid = 9;
    tx_rsp_bits.ptxn_uid = UINT64_C(96) + ((order)&low_mask(12));
    if (order == 0) {
      tx_rsp_valid = 1;
      tick_model();
      tx_rsp_valid = 0;
    }
    port_in.prx_udat.pvalid = 1;
    port_in.prx_udat.pbits.pdata_uid = 0;
    tx_rsp_valid = (order == 1);
    tick_model();
    port_in.prx_udat.pvalid = 0;
    tx_rsp_valid = (order == 2);
    tick_model();
    clear_flits();
  }

  for (int order = 0; order < 3; order++) {
    rx_snp_credit = 1;
    port_in.ptx_ursp.pcredit = 1;
    port_in.ptx_udat.pcredit = 1;
    tick_model();
    rx_snp_credit = 0;
    port_in.ptx_ursp.pcredit = 0;
    port_in.ptx_udat.pcredit = 0;
    port_in.prx_usnp.pvalid = 1;
    port_in.prx_usnp.pbits.popcode = UINT64_C(17);
    port_in.prx_usnp.pbits.psrc_uid = 9;
    port_in.prx_usnp.pbits.ptxn_uid = UINT64_C(771);
    port_in.prx_usnp.pbits.pfwd_unid_uor_upbha = 7;
    port_in.prx_usnp.pbits.pfwd_utxn_uid_uor_ustash_ulpid_uor_uvmid_uext =
        UINT64_C(1040) + ((order)&low_mask(12));
    tick_model();
    clear_flits();
    tx_rsp_bits.popcode = 9;
    tx_rsp_bits.psrc_uid = 5;
    tx_rsp_bits.ptgt_uid = 9;
    tx_rsp_bits.ptxn_uid = UINT64_C(771);
    tx_dat_bits.popcode = 4;
    tx_dat_bits.psrc_uid = 5;
    tx_dat_bits.ptgt_uid = 7;
    tx_dat_bits.ptxn_uid = UINT64_C(1040) + ((order)&low_mask(12));
    tx_rsp_valid = (order != 2);
    tx_dat_valid = (order != 0);
    tick_model();
    tx_rsp_valid = (order == 2);
    tx_dat_valid = (order == 0);
    tick_model();
    clear_flits();
  }

  for (int invalid = 0; invalid < 2; invalid++) {
    port_in.ptx_ureq.pcredit = 1;
    rx_rsp_credit = 1;
    tick_model();
    port_in.ptx_ureq.pcredit = 0;
    rx_rsp_credit = 0;
    tx_req_valid = 1;
    tx_req_bits = {};
    tx_req_bits.popcode = UINT64_C(27);
    tx_req_bits.psrc_uid = 5;
    tx_req_bits.ptgt_uid = 9;
    tx_req_bits.ptxn_uid = UINT64_C(546);
    tx_req_bits.psize_uor_unum_ureq = 6;
    tick_model();
    clear_flits();
    if (!EARLY_COPYBACK) {
      port_in.prx_ursp.pvalid = 1;
      port_in.prx_ursp.pbits = {};
      port_in.prx_ursp.pbits.popcode = 5;
      port_in.prx_ursp.pbits.psrc_uid = 9;
      port_in.prx_ursp.pbits.ptgt_uid = 5;
      port_in.prx_ursp.pbits.ptxn_uid = UINT64_C(546);
      port_in.prx_ursp.pbits.pdbid_uor_ugroup_uid = UINT64_C(85);
      tick_model();
      clear_flits();
    }
    for (int packet = 3; packet >= 0; packet--) {
      port_in.ptx_udat.pcredit = 1;
      tick_model();
      port_in.ptx_udat.pcredit = 0;
      tx_dat_valid = 1;
      tx_dat_bits = {};
      tx_dat_bits.popcode = 2;
      tx_dat_bits.psrc_uid = 5;
      tx_dat_bits.ptgt_uid = 9;
      tx_dat_bits.ptxn_uid = UINT64_C(85);
      tx_dat_bits.pdata_uid = ((packet)&low_mask(2));
      tx_dat_bits.presp = invalid != 0 ? 0 : 6;
      tx_dat_bits.pbyte_uenable = invalid != 0 ? 0 : UINT64_MAX;
      tx_dat_bits.pdata = wide(invalid != 0 ? 0 : packet + 1);
      tick_model();
      clear_flits();
    }
  }
  tx_link_active_request = UINT64_C(0);
  port_in.prx_ulink_uactive_urequest = UINT64_C(0);
  tick_model();
  port_in.ptx_ulink_uactive_uack = UINT64_C(0);
  rx_link_active_ack = UINT64_C(0);
  tick_model();
}

void early_ack() {
  reset = 1;

  reset = UINT64_C(1);
  tx_link_active_request = UINT64_C(0);
  rx_link_active_ack = UINT64_C(0);
  tx_req_valid = UINT64_C(0);
  tx_req_bits = {};
  tx_rsp_valid = UINT64_C(0);
  tx_rsp_bits = {};
  tx_dat_valid = UINT64_C(0);
  tx_dat_bits = {};
  rx_rsp_credit = UINT64_C(0);
  rx_dat_credit = UINT64_C(0);
  rx_snp_credit = UINT64_C(0);
  port_in = {};
  tick_model();

  reset = UINT64_C(0);
  tx_link_active_request = UINT64_C(1);
  port_in.prx_ulink_uactive_urequest = UINT64_C(1);
  tick_model();
  port_in.ptx_ulink_uactive_uack = UINT64_C(1);
  rx_link_active_ack = UINT64_C(1);
  tick_model();

  port_in.ptx_ursp.pcredit = UINT64_C(1);
  tick_model();
  port_in.ptx_ursp.pcredit = UINT64_C(0);
  tx_rsp_valid = UINT64_C(1);
  tx_rsp_bits.popcode = UINT64_C(2);
  tx_rsp_bits.psrc_uid = UINT64_C(5);
  tx_rsp_bits.ptgt_uid = UINT64_C(9);
  tx_rsp_bits.ptxn_uid = UINT64_C(85);
  tick_model();
}
int main() {
  return run_test([] {
    run_case();
    dut = Model{};
    EARLY_COPYBACK = 1;
    expect_failure("chi_coherent_copyback_data_has_grant", run_case);
    dut = Model{};
    expect_failure("chi_coherent_comp_ack_has_read_data", early_ack);
  });
}
