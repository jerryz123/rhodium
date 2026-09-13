// Simulates legal initial-profile CHI read and write transactions.
// SPDX-License-Identifier: Apache-2.0
module chi_transaction_tb;
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

  task clear_flits;
    begin
      tx_req_valid = 1'b0;
      tx_req_bits = '0;
      tx_dat_valid = 1'b0;
      tx_dat_bits = '0;
      port_in.rx_rsp.valid = 1'b0;
      port_in.rx_rsp.bits = '0;
      port_in.rx_dat.valid = 1'b0;
      port_in.rx_dat.bits = '0;
    end
  endtask

  initial begin
    clock = 1'b0;
    reset = 1'b1;
    tx_link_active_request = 1'b0;
    rx_link_active_ack = 1'b0;
    rx_rsp_credit = 1'b0;
    rx_dat_credit = 1'b0;
    port_in = '0;
    clear_flits();
    tick();

    reset = 1'b0;
    tx_link_active_request = 1'b1;
    port_in.rx_link_active_request = 1'b1;
    tick();
    port_in.tx_link_active_ack = 1'b1;
    rx_link_active_ack = 1'b1;
    tick();

    port_in.tx_req.credit = 1'b1;
    rx_dat_credit = 1'b1;
    tick();
    port_in.tx_req.credit = 1'b0;
    rx_dat_credit = 1'b0;
    tx_req_valid = 1'b1;
    tx_req_bits.opcode = 7'h04;
    tx_req_bits.src_id = 7'h03;
    tx_req_bits.tgt_id = 7'h09;
    tx_req_bits.txn_id = 12'h101;
    tx_req_bits.size_or_num_req = 6'h04;
    tick();
    clear_flits();

    port_in.rx_dat.valid = 1'b1;
    port_in.rx_dat.bits.opcode = 4'h4;
    port_in.rx_dat.bits.src_id = 7'h09;
    port_in.rx_dat.bits.tgt_id = 7'h03;
    port_in.rx_dat.bits.txn_id = 12'h101;
    tick();
    clear_flits();

    port_in.tx_req.credit = 1'b1;
    rx_rsp_credit = 1'b1;
    tick();
    port_in.tx_req.credit = 1'b0;
    rx_rsp_credit = 1'b0;
    tx_req_valid = 1'b1;
    tx_req_bits.opcode = 7'h1d;
    tx_req_bits.src_id = 7'h03;
    tx_req_bits.tgt_id = 7'h09;
    tx_req_bits.txn_id = 12'h202;
    tx_req_bits.size_or_num_req = 6'h04;
    tick();
    clear_flits();

    port_in.rx_rsp.valid = 1'b1;
    port_in.rx_rsp.bits.opcode = 5'h06;
    port_in.rx_rsp.bits.src_id = 7'h09;
    port_in.rx_rsp.bits.tgt_id = 7'h03;
    port_in.rx_rsp.bits.txn_id = 12'h202;
    port_in.rx_rsp.bits.dbid_or_group_id = 12'h055;
    tick();
    clear_flits();

    port_in.tx_dat.credit = 1'b1;
    rx_rsp_credit = 1'b1;
    tick();
    port_in.tx_dat.credit = 1'b0;
    rx_rsp_credit = 1'b0;
    tx_dat_valid = 1'b1;
    tx_dat_bits.opcode = 4'h3;
    tx_dat_bits.src_id = 7'h03;
    tx_dat_bits.tgt_id = 7'h09;
    tx_dat_bits.txn_id = 12'h055;
    tx_dat_bits.byte_enable = 16'hffff;
    tick();
    clear_flits();

    port_in.rx_rsp.valid = 1'b1;
    port_in.rx_rsp.bits.opcode = 5'h04;
    port_in.rx_rsp.bits.src_id = 7'h09;
    port_in.rx_rsp.bits.tgt_id = 7'h03;
    port_in.rx_rsp.bits.txn_id = 12'h202;
    port_in.rx_rsp.bits.dbid_or_group_id = 12'h055;
    tick();
    clear_flits();

    tx_link_active_request = 1'b0;
    port_in.rx_link_active_request = 1'b0;
    tick();
    port_in.tx_link_active_ack = 1'b0;
    rx_link_active_ack = 1'b0;
    tick();

    $display("CHI transaction monitor simulation passed");
    $finish;
  end
endmodule
