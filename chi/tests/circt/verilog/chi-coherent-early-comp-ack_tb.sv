// Confirms that an RN-F completion acknowledgement requires completed read data.
// SPDX-License-Identifier: Apache-2.0
module chi_coherent_early_comp_ack_tb;
  typedef struct packed { logic credit; } credit_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_forward_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_forward_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_forward_t;
  typedef struct packed { logic valid; CHISnpFlit bits; } snp_forward_t;
  typedef struct packed {
    logic tx_link_active_request;
    logic rx_link_active_ack;
    req_forward_t tx_req;
    rsp_forward_t tx_rsp;
    dat_forward_t tx_dat;
    credit_t rx_rsp;
    credit_t rx_dat;
    credit_t rx_snp;
  } node_to_icn_t;
  typedef struct packed {
    credit_t tx_req;
    credit_t tx_rsp;
    credit_t tx_dat;
    logic tx_link_active_ack;
    logic rx_link_active_request;
    rsp_forward_t rx_rsp;
    dat_forward_t rx_dat;
    snp_forward_t rx_snp;
  } icn_to_node_t;

  logic clock;
  logic reset;
  logic tx_link_active_request;
  logic rx_link_active_ack;
  logic tx_req_valid;
  CHIReqFlit tx_req_bits;
  logic tx_rsp_valid;
  CHIRspFlit tx_rsp_bits;
  logic tx_dat_valid;
  CHIDatFlit tx_dat_bits;
  logic rx_rsp_credit;
  logic rx_dat_credit;
  logic rx_snp_credit;
  icn_to_node_t port_in;
  node_to_icn_t port_out;

  MonitoredInitialCHIRNF dut (.*);

  task tick;
    begin
      #1 clock = 1'b1;
      #1 clock = 1'b0;
    end
  endtask

  initial begin
    clock = 1'b0;
    reset = 1'b1;
    tx_link_active_request = 1'b0;
    rx_link_active_ack = 1'b0;
    tx_req_valid = 1'b0;
    tx_req_bits = '0;
    tx_rsp_valid = 1'b0;
    tx_rsp_bits = '0;
    tx_dat_valid = 1'b0;
    tx_dat_bits = '0;
    rx_rsp_credit = 1'b0;
    rx_dat_credit = 1'b0;
    rx_snp_credit = 1'b0;
    port_in = '0;
    tick();

    reset = 1'b0;
    tx_link_active_request = 1'b1;
    port_in.rx_link_active_request = 1'b1;
    tick();
    port_in.tx_link_active_ack = 1'b1;
    rx_link_active_ack = 1'b1;
    tick();

    port_in.tx_rsp.credit = 1'b1;
    tick();
    port_in.tx_rsp.credit = 1'b0;
    tx_rsp_valid = 1'b1;
    tx_rsp_bits.opcode = 5'h02;
    tx_rsp_bits.src_id = 7'h05;
    tx_rsp_bits.tgt_id = 7'h09;
    tx_rsp_bits.txn_id = 12'h055;
    tick();

    $fatal(1, "expected coherent completion acknowledgement assertion");
  end
endmodule
