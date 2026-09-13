// Simulates legal initial-profile CHI transactions at a Subordinate Node.
// SPDX-License-Identifier: Apache-2.0
module chi_transaction_sn_tb;
  typedef struct packed { logic credit; } credit_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_forward_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_forward_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_forward_t;
  typedef struct packed {
    logic tx_link_active_request;
    logic rx_link_active_ack;
    rsp_forward_t tx_rsp;
    dat_forward_t tx_dat;
    credit_t rx_req;
    credit_t rx_dat;
  } node_to_icn_t;
  typedef struct packed {
    credit_t tx_rsp;
    credit_t tx_dat;
    logic tx_link_active_ack;
    logic rx_link_active_request;
    req_forward_t rx_req;
    dat_forward_t rx_dat;
  } icn_to_node_t;

  logic clock;
  logic reset;
  logic tx_link_active_request;
  logic rx_link_active_ack;
  logic tx_rsp_valid;
  CHIRspFlit tx_rsp_bits;
  logic tx_dat_valid;
  CHIDatFlit tx_dat_bits;
  logic rx_req_credit;
  logic rx_dat_credit;
  icn_to_node_t port_in;
  node_to_icn_t port_out;

  MonitoredInitialCHISN dut (.*);

  task tick;
    begin
      #1 clock = 1'b1;
      #1 clock = 1'b0;
    end
  endtask

  task clear_flits;
    begin
      tx_rsp_valid = 1'b0;
      tx_rsp_bits = '0;
      tx_dat_valid = 1'b0;
      tx_dat_bits = '0;
      port_in.rx_req.valid = 1'b0;
      port_in.rx_req.bits = '0;
      port_in.rx_dat.valid = 1'b0;
      port_in.rx_dat.bits = '0;
    end
  endtask

  initial begin
    clock = 1'b0;
    reset = 1'b1;
    tx_link_active_request = 1'b0;
    rx_link_active_ack = 1'b0;
    rx_req_credit = 1'b0;
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

    rx_req_credit = 1'b1;
    port_in.tx_dat.credit = 1'b1;
    tick();
    rx_req_credit = 1'b0;
    port_in.tx_dat.credit = 1'b0;
    port_in.rx_req.valid = 1'b1;
    port_in.rx_req.bits.opcode = 7'h04;
    port_in.rx_req.bits.src_id = 7'h07;
    port_in.rx_req.bits.tgt_id = 7'h09;
    port_in.rx_req.bits.txn_id = 12'h301;
    port_in.rx_req.bits.return_nid_or_stash_nid_or_data_target = 7'h03;
    port_in.rx_req.bits.return_txn_id_or_stash_lpid = 12'h101;
    port_in.rx_req.bits.size_or_num_req = 6'h04;
    tick();
    clear_flits();

    tx_dat_valid = 1'b1;
    tx_dat_bits.opcode = 4'h4;
    tx_dat_bits.src_id = 7'h09;
    tx_dat_bits.tgt_id = 7'h03;
    tx_dat_bits.txn_id = 12'h101;
    tick();
    clear_flits();

    rx_req_credit = 1'b1;
    port_in.tx_rsp.credit = 1'b1;
    tick();
    rx_req_credit = 1'b0;
    port_in.tx_rsp.credit = 1'b0;
    port_in.rx_req.valid = 1'b1;
    port_in.rx_req.bits.opcode = 7'h1c;
    port_in.rx_req.bits.src_id = 7'h07;
    port_in.rx_req.bits.tgt_id = 7'h09;
    port_in.rx_req.bits.txn_id = 12'h302;
    port_in.rx_req.bits.size_or_num_req = 6'h04;
    tick();
    clear_flits();

    tx_rsp_valid = 1'b1;
    tx_rsp_bits.opcode = 5'h06;
    tx_rsp_bits.src_id = 7'h09;
    tx_rsp_bits.tgt_id = 7'h07;
    tx_rsp_bits.txn_id = 12'h302;
    tx_rsp_bits.dbid_or_group_id = 12'h044;
    tick();
    clear_flits();

    rx_dat_credit = 1'b1;
    port_in.tx_rsp.credit = 1'b1;
    tick();
    rx_dat_credit = 1'b0;
    port_in.tx_rsp.credit = 1'b0;
    port_in.rx_dat.valid = 1'b1;
    port_in.rx_dat.bits.opcode = 4'h3;
    port_in.rx_dat.bits.src_id = 7'h07;
    port_in.rx_dat.bits.tgt_id = 7'h09;
    port_in.rx_dat.bits.txn_id = 12'h044;
    port_in.rx_dat.bits.byte_enable = 16'h00ff;
    tick();
    clear_flits();

    tx_rsp_valid = 1'b1;
    tx_rsp_bits.opcode = 5'h04;
    tx_rsp_bits.src_id = 7'h09;
    tx_rsp_bits.tgt_id = 7'h07;
    tx_rsp_bits.txn_id = 12'h302;
    tx_rsp_bits.dbid_or_group_id = 12'h044;
    tick();
    clear_flits();

    tx_link_active_request = 1'b0;
    port_in.rx_link_active_request = 1'b0;
    tick();
    port_in.tx_link_active_ack = 1'b0;
    rx_link_active_ack = 1'b0;
    tick();

    $display("CHI subordinate transaction monitor simulation passed");
    $finish;
  end
endmodule
