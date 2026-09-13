// Checks coherent reads, copyback grants and packets, and forwarded snoop completion.
// SPDX-License-Identifier: Apache-2.0
module chi_coherent_tb #(parameter bit EARLY_COPYBACK = 0);
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

  task clear_flits;
    begin
      tx_req_valid = 1'b0;
      tx_req_bits = '0;
      tx_rsp_valid = 1'b0;
      tx_rsp_bits = '0;
      tx_dat_valid = 1'b0;
      tx_dat_bits = '0;
      port_in.rx_rsp.valid = 1'b0;
      port_in.rx_rsp.bits = '0;
      port_in.rx_dat.valid = 1'b0;
      port_in.rx_dat.bits = '0;
      port_in.rx_snp.valid = 1'b0;
      port_in.rx_snp.bits = '0;
    end
  endtask

  initial begin
    clock = 1'b0;
    reset = 1'b1;
    tx_link_active_request = 1'b0;
    rx_link_active_ack = 1'b0;
    rx_rsp_credit = 1'b0;
    rx_dat_credit = 1'b0;
    rx_snp_credit = 1'b0;
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
    tick();
    rx_dat_credit = 1'b0;
    tx_req_valid = 1'b1;
    tx_req_bits.opcode = 7'h01;
    tx_req_bits.src_id = 7'h05;
    tx_req_bits.tgt_id = 7'h09;
    tx_req_bits.txn_id = 12'h101;
    tx_req_bits.size_or_num_req = 6'h05;
    tx_req_bits.exp_comp_ack = 1'b1;
    tx_req_bits.allow_retry = 1'b1;
    tick();
    clear_flits();

    port_in.rx_dat.valid = 1'b1;
    port_in.rx_dat.bits.opcode = 4'h4;
    port_in.rx_dat.bits.src_id = 7'h09;
    port_in.rx_dat.bits.tgt_id = 7'h05;
    port_in.rx_dat.bits.txn_id = 12'h101;
    port_in.rx_dat.bits.home_nid_or_pbha_or_mismatched_mecid = 7'h09;
    port_in.rx_dat.bits.dbid_or_mecid = 16'h0055;
    port_in.rx_dat.bits.data_id = 2'h1;
    tick();
    clear_flits();

    port_in.tx_rsp.credit = 1'b1;
    tick();
    port_in.tx_rsp.credit = 1'b0;
    tx_rsp_valid = 1'b1;
    tx_rsp_bits.opcode = 5'h02;
    tx_rsp_bits.src_id = 7'h05;
    tx_rsp_bits.tgt_id = 7'h09;
    tx_rsp_bits.txn_id = 12'h055;
    tick();
    clear_flits();

    port_in.rx_dat.valid = 1'b1;
    port_in.rx_dat.bits.opcode = 4'h4;
    port_in.rx_dat.bits.src_id = 7'h09;
    port_in.rx_dat.bits.tgt_id = 7'h05;
    port_in.rx_dat.bits.txn_id = 12'h101;
    port_in.rx_dat.bits.home_nid_or_pbha_or_mismatched_mecid = 7'h09;
    port_in.rx_dat.bits.dbid_or_mecid = 16'h0055;
    port_in.rx_dat.bits.data_id = 2'h0;
    tick();
    clear_flits();

    rx_snp_credit = 1'b1;
    tick();
    rx_snp_credit = 1'b0;
    port_in.rx_snp.valid = 1'b1;
    port_in.rx_snp.bits.opcode = 5'h07;
    port_in.rx_snp.bits.src_id = 7'h09;
    port_in.rx_snp.bits.txn_id = 12'h202;
    tick();
    clear_flits();

    port_in.tx_rsp.credit = 1'b1;
    tick();
    port_in.tx_rsp.credit = 1'b0;
    tx_rsp_valid = 1'b1;
    tx_rsp_bits.opcode = 5'h01;
    tx_rsp_bits.src_id = 7'h05;
    tx_rsp_bits.tgt_id = 7'h09;
    tx_rsp_bits.txn_id = 12'h202;
    tick();
    clear_flits();

    rx_snp_credit = 1'b1;
    tick();
    rx_snp_credit = 1'b0;
    port_in.rx_snp.valid = 1'b1;
    port_in.rx_snp.bits.opcode = 5'h11;
    port_in.rx_snp.bits.src_id = 7'h09;
    port_in.rx_snp.bits.txn_id = 12'h303;
    port_in.rx_snp.bits.fwd_nid_or_pbha = 7'h07;
    port_in.rx_snp.bits.fwd_txn_id_or_stash_lpid_or_vmid_ext = 12'h404;
    tick();
    clear_flits();

    port_in.tx_rsp.credit = 1'b1;
    port_in.tx_dat.credit = 1'b1;
    tick();
    port_in.tx_rsp.credit = 1'b0;
    port_in.tx_dat.credit = 1'b0;
    tx_rsp_valid = 1'b1;
    tx_rsp_bits.opcode = 5'h09;
    tx_rsp_bits.src_id = 7'h05;
    tx_rsp_bits.tgt_id = 7'h09;
    tx_rsp_bits.txn_id = 12'h303;
    tick();
    clear_flits();

    tx_dat_valid = 1'b1;
    tx_dat_bits.opcode = 4'h4;
    tx_dat_bits.src_id = 7'h05;
    tx_dat_bits.tgt_id = 7'h07;
    tx_dat_bits.txn_id = 12'h404;
    tick();
    clear_flits();

    // Reuse identities after retirement; acknowledge before, with, and after
    // the last data beat. The first beat establishes HomeID and CompAck ID.
    for (int order = 0; order < 3; order++) begin
      port_in.tx_req.credit = 1;
      port_in.tx_rsp.credit = 1;
      rx_dat_credit = 1;
      tick();
      port_in.tx_req.credit = 0;
      port_in.tx_rsp.credit = 0;
      tick(); // Supply the second receive credit.
      rx_dat_credit = 0;
      tx_req_valid = 1;
      tx_req_bits.opcode = 7'h01;
      tx_req_bits.src_id = 5;
      tx_req_bits.tgt_id = 9;
      tx_req_bits.txn_id = 12'h101;
      tx_req_bits.size_or_num_req = 5;
      tx_req_bits.exp_comp_ack = 1;
      tx_req_bits.allow_retry = 1;
      tick();
      clear_flits();
      port_in.rx_dat.valid = 1;
      port_in.rx_dat.bits.opcode = 4;
      port_in.rx_dat.bits.src_id = 9;
      port_in.rx_dat.bits.tgt_id = 5;
      port_in.rx_dat.bits.txn_id = 12'h101;
      port_in.rx_dat.bits.home_nid_or_pbha_or_mismatched_mecid = 9;
      port_in.rx_dat.bits.dbid_or_mecid = 16'h0060 + 16'(order);
      port_in.rx_dat.bits.data_id = 1;
      tick();
      port_in.rx_dat.valid = 0;
      tx_rsp_bits.opcode = 2;
      tx_rsp_bits.src_id = 5;
      tx_rsp_bits.tgt_id = 9;
      tx_rsp_bits.txn_id = 12'h060 + 12'(order);
      if (order == 0) begin
        tx_rsp_valid = 1;
        tick();
        tx_rsp_valid = 0;
      end
      port_in.rx_dat.valid = 1;
      port_in.rx_dat.bits.data_id = 0;
      tx_rsp_valid = (order == 1);
      tick();
      port_in.rx_dat.valid = 0;
      tx_rsp_valid = (order == 2);
      tick();
      clear_flits();
    end

    // The forwarded response and forwarded DAT are independent obligations.
    // Reusing this snoop identity also checks that allocation clears receipt bits.
    for (int order = 0; order < 3; order++) begin
      rx_snp_credit = 1;
      port_in.tx_rsp.credit = 1;
      port_in.tx_dat.credit = 1;
      tick();
      rx_snp_credit = 0;
      port_in.tx_rsp.credit = 0;
      port_in.tx_dat.credit = 0;
      port_in.rx_snp.valid = 1;
      port_in.rx_snp.bits.opcode = 5'h11;
      port_in.rx_snp.bits.src_id = 9;
      port_in.rx_snp.bits.txn_id = 12'h303;
      port_in.rx_snp.bits.fwd_nid_or_pbha = 7;
      port_in.rx_snp.bits.fwd_txn_id_or_stash_lpid_or_vmid_ext = 12'h410 + 12'(order);
      tick();
      clear_flits();
      tx_rsp_bits.opcode = 9;
      tx_rsp_bits.src_id = 5;
      tx_rsp_bits.tgt_id = 9;
      tx_rsp_bits.txn_id = 12'h303;
      tx_dat_bits.opcode = 4;
      tx_dat_bits.src_id = 5;
      tx_dat_bits.tgt_id = 7;
      tx_dat_bits.txn_id = 12'h410 + 12'(order);
      tx_rsp_valid = (order != 2);
      tx_dat_valid = (order != 0);
      tick();
      tx_rsp_valid = (order == 2);
      tx_dat_valid = (order == 0);
      tick();
      clear_flits();
    end

    // Reuse the same TxnID and DBID after all four packets, including Invalid
    // copyback. Credited and ready-valid attachments observe the same events.
    for (int invalid = 0; invalid < 2; invalid++) begin
      port_in.tx_req.credit = 1; rx_rsp_credit = 1; tick();
      port_in.tx_req.credit = 0; rx_rsp_credit = 0;
      tx_req_valid = 1; tx_req_bits = '0;
      tx_req_bits.opcode = 7'h1b; tx_req_bits.src_id = 5; tx_req_bits.tgt_id = 9;
      tx_req_bits.txn_id = 12'h222; tx_req_bits.size_or_num_req = 6;
      tick(); clear_flits();
      if (!EARLY_COPYBACK) begin
        port_in.rx_rsp.valid = 1; port_in.rx_rsp.bits = '0;
        port_in.rx_rsp.bits.opcode = 5; port_in.rx_rsp.bits.src_id = 9;
        port_in.rx_rsp.bits.tgt_id = 5; port_in.rx_rsp.bits.txn_id = 12'h222;
        port_in.rx_rsp.bits.dbid_or_group_id = 12'h055;
        tick(); clear_flits();
      end
      for (int packet = 3; packet >= 0; packet--) begin
        port_in.tx_dat.credit = 1; tick(); port_in.tx_dat.credit = 0;
        tx_dat_valid = 1; tx_dat_bits = '0;
        tx_dat_bits.opcode = 2; tx_dat_bits.src_id = 5; tx_dat_bits.tgt_id = 9;
        tx_dat_bits.txn_id = 12'h055; tx_dat_bits.data_id = 2'(packet);
        tx_dat_bits.resp = invalid != 0 ? 0 : 6;
        tx_dat_bits.byte_enable = invalid != 0 ? 0 : '1;
        tx_dat_bits.data = invalid != 0 ? 0 : 128'(packet + 1);
        tick(); clear_flits();
      end
    end
    tx_link_active_request = 1'b0;
    port_in.rx_link_active_request = 1'b0;
    tick();
    port_in.tx_link_active_ack = 1'b0;
    rx_link_active_ack = 1'b0;
    tick();

    $display("CHI coherent RN-F monitor simulation passed");
    $finish;
  end
endmodule

module chi_copyback_early_data_tb;
  chi_coherent_tb #(.EARLY_COPYBACK(1)) test();
endmodule
