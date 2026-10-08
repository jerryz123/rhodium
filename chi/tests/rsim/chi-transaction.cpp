// Simulates legal initial-profile CHI read and write transactions.
// SPDX-License-Identifier: Apache-2.0
#include "request.hpp"
#include "test.hpp"

void clear_flits() {
  {
    tx_req_valid = UINT64_C(0);
    tx_req_bits = {};
    tx_dat_valid = UINT64_C(0);
    tx_dat_bits = {};
    port_in.prx_ursp.pvalid = UINT64_C(0);
    port_in.prx_ursp.pbits = {};
    port_in.prx_udat.pvalid = UINT64_C(0);
    port_in.prx_udat.pbits = {};
  }
}

void run_case() {

  reset = UINT64_C(1);
  tx_link_active_request = UINT64_C(0);
  rx_link_active_ack = UINT64_C(0);
  rx_rsp_credit = UINT64_C(0);
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

  port_in.ptx_ureq.pcredit = UINT64_C(1);
  rx_dat_credit = UINT64_C(1);
  tick_model();
  port_in.ptx_ureq.pcredit = UINT64_C(0);
  rx_dat_credit = UINT64_C(0);
  tx_req_valid = UINT64_C(1);
  tx_req_bits.popcode = UINT64_C(4);
  tx_req_bits.psrc_uid = UINT64_C(3);
  tx_req_bits.ptgt_uid = UINT64_C(9);
  tx_req_bits.ptxn_uid = UINT64_C(257);
  tx_req_bits.psize_uor_unum_ureq = UINT64_C(4);
  tick_model();
  clear_flits();

  port_in.prx_udat.pvalid = UINT64_C(1);
  port_in.prx_udat.pbits.popcode = UINT64_C(4);
  port_in.prx_udat.pbits.psrc_uid = UINT64_C(9);
  port_in.prx_udat.pbits.ptgt_uid = UINT64_C(3);
  port_in.prx_udat.pbits.ptxn_uid = UINT64_C(257);
  tick_model();
  clear_flits();

  port_in.ptx_ureq.pcredit = UINT64_C(1);
  rx_rsp_credit = UINT64_C(1);
  tick_model();
  port_in.ptx_ureq.pcredit = UINT64_C(0);
  rx_rsp_credit = UINT64_C(0);
  tx_req_valid = UINT64_C(1);
  tx_req_bits.popcode = UINT64_C(29);
  tx_req_bits.psrc_uid = UINT64_C(3);
  tx_req_bits.ptgt_uid = UINT64_C(9);
  tx_req_bits.ptxn_uid = UINT64_C(514);
  tx_req_bits.psize_uor_unum_ureq = UINT64_C(4);
  tick_model();
  clear_flits();

  port_in.prx_ursp.pvalid = UINT64_C(1);
  port_in.prx_ursp.pbits.popcode = UINT64_C(6);
  port_in.prx_ursp.pbits.psrc_uid = UINT64_C(9);
  port_in.prx_ursp.pbits.ptgt_uid = UINT64_C(3);
  port_in.prx_ursp.pbits.ptxn_uid = UINT64_C(514);
  port_in.prx_ursp.pbits.pdbid_uor_ugroup_uid = UINT64_C(85);
  tick_model();
  clear_flits();

  port_in.ptx_udat.pcredit = UINT64_C(1);
  rx_rsp_credit = UINT64_C(1);
  tick_model();
  port_in.ptx_udat.pcredit = UINT64_C(0);
  rx_rsp_credit = UINT64_C(0);
  tx_dat_valid = UINT64_C(1);
  tx_dat_bits.popcode = UINT64_C(3);
  tx_dat_bits.psrc_uid = UINT64_C(3);
  tx_dat_bits.ptgt_uid = UINT64_C(9);
  tx_dat_bits.ptxn_uid = UINT64_C(85);
  tx_dat_bits.pbyte_uenable = UINT64_C(65535);
  tick_model();
  clear_flits();

  port_in.prx_ursp.pvalid = UINT64_C(1);
  port_in.prx_ursp.pbits.popcode = UINT64_C(4);
  port_in.prx_ursp.pbits.psrc_uid = UINT64_C(9);
  port_in.prx_ursp.pbits.ptgt_uid = UINT64_C(3);
  port_in.prx_ursp.pbits.ptxn_uid = UINT64_C(514);
  port_in.prx_ursp.pbits.pdbid_uor_ugroup_uid = UINT64_C(85);
  tick_model();
  clear_flits();

  tx_link_active_request = UINT64_C(0);
  port_in.prx_ulink_uactive_urequest = UINT64_C(0);
  tick_model();
  port_in.ptx_ulink_uactive_uack = UINT64_C(0);
  rx_link_active_ack = UINT64_C(0);
  tick_model();
}

void duplicate_txn() {

  reset = UINT64_C(1);
  tx_link_active_request = UINT64_C(0);
  rx_link_active_ack = UINT64_C(0);
  tx_req_valid = UINT64_C(0);
  tx_req_bits = {};
  tx_dat_valid = UINT64_C(0);
  tx_dat_bits = {};
  rx_rsp_credit = UINT64_C(0);
  rx_dat_credit = UINT64_C(0);
  port_in = {};
  tick_model();

  reset = UINT64_C(0);
  tx_link_active_request = UINT64_C(1);
  port_in.prx_ulink_uactive_urequest = UINT64_C(1);
  tick_model();
  port_in.ptx_ulink_uactive_uack = UINT64_C(1);
  rx_link_active_ack = UINT64_C(1);
  tick_model();

  port_in.ptx_ureq.pcredit = UINT64_C(1);
  tick_model();
  port_in.ptx_ureq.pcredit = UINT64_C(0);
  tx_req_valid = UINT64_C(1);
  tx_req_bits.popcode = UINT64_C(4);
  tx_req_bits.psrc_uid = UINT64_C(3);
  tx_req_bits.ptgt_uid = UINT64_C(9);
  tx_req_bits.ptxn_uid = UINT64_C(291);
  tx_req_bits.psize_uor_unum_ureq = UINT64_C(4);
  tick_model();

  tx_req_valid = UINT64_C(0);
  port_in.ptx_ureq.pcredit = UINT64_C(1);
  tick_model();
  port_in.ptx_ureq.pcredit = UINT64_C(0);
  tx_req_valid = UINT64_C(1);
  tick_model();
}

void early_data() {

  reset = UINT64_C(1);
  tx_link_active_request = UINT64_C(0);
  rx_link_active_ack = UINT64_C(0);
  tx_req_valid = UINT64_C(0);
  tx_req_bits = {};
  tx_dat_valid = UINT64_C(0);
  tx_dat_bits = {};
  rx_rsp_credit = UINT64_C(0);
  rx_dat_credit = UINT64_C(0);
  port_in = {};
  tick_model();

  reset = UINT64_C(0);
  tx_link_active_request = UINT64_C(1);
  port_in.prx_ulink_uactive_urequest = UINT64_C(1);
  tick_model();
  port_in.ptx_ulink_uactive_uack = UINT64_C(1);
  rx_link_active_ack = UINT64_C(1);
  tick_model();

  port_in.ptx_ureq.pcredit = UINT64_C(1);
  tick_model();
  port_in.ptx_ureq.pcredit = UINT64_C(0);
  tx_req_valid = UINT64_C(1);
  tx_req_bits.popcode = UINT64_C(29);
  tx_req_bits.psrc_uid = UINT64_C(3);
  tx_req_bits.ptgt_uid = UINT64_C(9);
  tx_req_bits.ptxn_uid = UINT64_C(546);
  tx_req_bits.psize_uor_unum_ureq = UINT64_C(4);
  tick_model();

  tx_req_valid = UINT64_C(0);
  port_in.ptx_udat.pcredit = UINT64_C(1);
  tick_model();
  port_in.ptx_udat.pcredit = UINT64_C(0);
  tx_dat_valid = UINT64_C(1);
  tx_dat_bits.popcode = UINT64_C(3);
  tx_dat_bits.psrc_uid = UINT64_C(3);
  tx_dat_bits.ptgt_uid = UINT64_C(9);
  tx_dat_bits.ptxn_uid = UINT64_C(68);
  tick_model();
}
int main() {
  return run_test([] {
    run_case();
    dut = Model{};
    expect_failure("chi_transaction_txn_id_unique", duplicate_txn);
    dut = Model{};
    expect_failure("chi_transaction_write_data_has_dbid", early_data);
  });
}
