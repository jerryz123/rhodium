// Simulates transparent CHI activation, credited request, response, and credit wiring.
// SPDX-License-Identifier: Apache-2.0
module chi_link_tb;
  typedef struct packed { logic credit; } credit_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_forward_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_forward_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_forward_t;
  typedef struct packed {
    logic tx_link_active_request;
    logic rx_link_active_ack;
    req_forward_t tx_req;
    rsp_forward_t tx_rsp;
    dat_forward_t tx_dat;
    credit_t rx_rsp;
    credit_t rx_dat;
  } node_to_icn_t;
  typedef struct packed {
    credit_t tx_req;
    credit_t tx_rsp;
    credit_t tx_dat;
    logic tx_link_active_ack;
    logic rx_link_active_request;
    rsp_forward_t rx_rsp;
    dat_forward_t rx_dat;
  } icn_to_node_t;

  node_to_icn_t ingress_in;
  icn_to_node_t egress_in;
  icn_to_node_t ingress_out;
  node_to_icn_t egress_out;

  CHIRNILinkAdapter dut (.*);

  initial begin
    ingress_in = '0;
    egress_in = '0;

    ingress_in.tx_link_active_request = 1'b1;
    ingress_in.rx_link_active_ack = 1'b1;
    ingress_in.tx_req.valid = 1'b1;
    ingress_in.tx_req.bits.opcode = 7'h04;
    ingress_in.tx_req.bits.address = 44'h123456789ab;
    ingress_in.rx_rsp.credit = 1'b1;
    egress_in.tx_req.credit = 1'b1;
    egress_in.tx_link_active_ack = 1'b1;
    egress_in.rx_link_active_request = 1'b1;
    egress_in.rx_rsp.valid = 1'b1;
    egress_in.rx_rsp.bits.opcode = 5'h04;
    egress_in.rx_rsp.bits.txn_id = 12'h5a5;
    #1;

    assert (egress_out === ingress_in)
      else $fatal(1, "node-to-ICN CHI link did not pass through exactly");
    assert (ingress_out === egress_in)
      else $fatal(1, "ICN-to-node CHI link did not pass through exactly");

    $display("CHI link simulation passed");
    $finish;
  end
endmodule
