// Violates the initial CHI transaction profile by sending write data before DBIDResp.
// SPDX-License-Identifier: Apache-2.0
module chi_transaction_early_data_tb;
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

  logic clock;
  logic reset;
  logic tx_link_active_request;
  logic rx_link_active_ack;
  logic tx_req_valid;
  CHIReqFlit tx_req_bits;
  logic tx_dat_valid;
  CHIDatFlit tx_dat_bits;
  logic rx_rsp_credit;
  logic rx_dat_credit;
  icn_to_node_t port_in;
  node_to_icn_t port_out;

  MonitoredInitialCHIRNI dut (.*);

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
    tx_dat_valid = 1'b0;
    tx_dat_bits = '0;
    rx_rsp_credit = 1'b0;
    rx_dat_credit = 1'b0;
    port_in = '0;
    tick();

    reset = 1'b0;
    tx_link_active_request = 1'b1;
    port_in.rx_link_active_request = 1'b1;
    tick();
    port_in.tx_link_active_ack = 1'b1;
    rx_link_active_ack = 1'b1;
    tick();

    port_in.tx_req.credit = 1'b1;
    tick();
    port_in.tx_req.credit = 1'b0;
    tx_req_valid = 1'b1;
    tx_req_bits.opcode = 7'h1d;
    tx_req_bits.src_id = 7'h03;
    tx_req_bits.tgt_id = 7'h09;
    tx_req_bits.txn_id = 12'h222;
    tx_req_bits.size_or_num_req = 6'h04;
    tick();

    tx_req_valid = 1'b0;
    port_in.tx_dat.credit = 1'b1;
    tick();
    port_in.tx_dat.credit = 1'b0;
    tx_dat_valid = 1'b1;
    tx_dat_bits.opcode = 4'h3;
    tx_dat_bits.src_id = 7'h03;
    tx_dat_bits.tgt_id = 7'h09;
    tx_dat_bits.txn_id = 12'h044;
    tick();
    $fatal(1, "early write data assertion did not fire");
  end
endmodule
