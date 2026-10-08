// Simulates legal activation, credit, request, response, and deactivation on a
// monitored CHI link.
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

void run_case() {

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
  rx_rsp_credit = UINT64_C(1);
  tick_model();

  port_in.ptx_ureq.pcredit = UINT64_C(0);
  rx_rsp_credit = UINT64_C(0);
  tx_req_valid = UINT64_C(1);
  tx_req_bits.popcode = UINT64_C(4);
  tx_req_bits.psrc_uid = UINT64_C(3);
  tx_req_bits.psize_uor_unum_ureq = UINT64_C(2);
  port_in.prx_ursp.pvalid = UINT64_C(1);
  port_in.prx_ursp.pbits.popcode = UINT64_C(4);
  port_in.prx_ursp.pbits.ptgt_uid = UINT64_C(3);
  tick_model();

  tx_req_valid = UINT64_C(0);
  port_in.prx_ursp.pvalid = UINT64_C(0);
  tx_link_active_request = UINT64_C(0);
  port_in.prx_ulink_uactive_urequest = UINT64_C(0);
  tick_model();

  port_in.ptx_ulink_uactive_uack = UINT64_C(0);
  rx_link_active_ack = UINT64_C(0);
  tick_model();
}

void unsupported_opcode() {
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
  port_in = {};
  tick_model();

  reset = UINT64_C(0);
  tx_link_active_request = UINT64_C(1);
  tick_model();
  port_in.ptx_ulink_uactive_uack = UINT64_C(1);
  tick_model();
  port_in.ptx_ureq.pcredit = UINT64_C(1);
  tick_model();

  port_in.ptx_ureq.pcredit = UINT64_C(0);
  tx_req_valid = UINT64_C(1);
  tx_req_bits.popcode = UINT64_C(29);
  tx_req_bits.psrc_uid = UINT64_C(3);
  tx_req_bits.psize_uor_unum_ureq = UINT64_C(2);
  tick_model();
}
int main() {
  return run_test([] {
    run_case();
    dut = Model{};
    expect_failure("chi_tx_req_opcode_supported", unsupported_opcode);
  });
}
