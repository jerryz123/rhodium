// Checks transaction monitoring under stalls, concurrent progress, multibeat retirement, reuse, and invalid associations.
// SPDX-License-Identifier: Apache-2.0
module chi_channel_monitor_test #(parameter MODE = 0);
  logic clock = 0, reset = 1;
  logic req_valid = 0, req_ready = 0;
  logic rsp_valid = 0, rsp_ready = 0;
  logic wdat_valid = 0, wdat_ready = 0;
  logic rdat_valid = 0, rdat_ready = 0;
  CHIReqFlit req_bits;
  CHIRspFlit rsp_bits;
  CHIDatFlit wdat_bits, rdat_bits;
  MonitoredCHIChannels dut (.*);

  task tick;
    #1 clock = 1;
    #1 clock = 0;
  endtask

  task request(input bit write_request, input logic [11:0] txn = 12'h101,
               input logic [5:0] size = 4);
    req_bits = '0;
    req_bits.opcode = write_request ? 7'h1d : 7'h04;
    req_bits.src_id = 3;
    req_bits.tgt_id = 9;
    req_bits.return_nid_or_stash_nid_or_data_target = 3;
    req_bits.return_txn_id_or_stash_lpid = txn;
    req_bits.txn_id = txn;
    req_bits.size_or_num_req = size;
    req_valid = 1;
    req_ready = 0;
    repeat (3) tick();
    req_ready = 1;
    tick();
    req_valid = 0;
  endtask

  task read_response;
    rdat_bits = '0;
    rdat_bits.opcode = 4;
    rdat_bits.src_id = 9;
    rdat_bits.tgt_id = 3;
    rdat_bits.txn_id = 12'h101;
    rdat_valid = 1;
    rdat_ready = 0;
    repeat (3) tick();
    rdat_ready = 1;
    tick();
    rdat_valid = 0;
  endtask

  initial begin
    req_bits = '0;
    rsp_bits = '0;
    wdat_bits = '0;
    rdat_bits = '0;
    tick();
    reset = 0;
    request(0);
    if (MODE == 1) begin
      req_valid = 1;
      tick(); // Accepted duplicate must fail.
    end
    if (MODE == 2) begin
      rdat_bits.opcode = 4;
      rdat_bits.src_id = 9;
      rdat_bits.tgt_id = 4;
      rdat_bits.txn_id = 12'h101;
      rdat_valid = 1;
      rdat_ready = 1;
      tick();
    end
    read_response();
    request(1); // Reuse the retired TxnID.
    wdat_bits.opcode = 3;
    wdat_bits.src_id = 3;
    wdat_bits.tgt_id = 9;
    wdat_bits.txn_id = 12'h055;
    wdat_bits.byte_enable = '1;
    wdat_valid = 1;
    wdat_ready = 0;
    repeat (3) tick(); // Offered DAT before DBID is not a transfer.
    if (MODE == 3) begin
      wdat_ready = 1;
      tick(); // Accepted early DAT must fail.
    end
    rsp_bits.opcode = 6;
    rsp_bits.src_id = 9;
    rsp_bits.tgt_id = 3;
    rsp_bits.txn_id = 12'h101;
    rsp_bits.dbid_or_group_id = 12'h055;
    rsp_valid = 1;
    repeat (3) tick();
    rsp_ready = 1;
    tick();
    rsp_valid = 0;
    wdat_ready = 1;
    tick();
    wdat_valid = 0;
    rsp_bits.opcode = 4;
    rsp_valid = 1;
    rsp_ready = 0;
    repeat (3) tick();
    rsp_ready = 1;
    tick();
    rsp_valid = 0;
    request(0);
    reset = 1; // Reset discards the pending transaction.
    tick();
    reset = 0;
    request(0);
    read_response();
    // Independent entries may progress on RSP and DAT together. Keep a
    // two-packet write live until both out-of-order packets have arrived.
    request(0);
    request(1, 12'h202, 5);
    rsp_bits.opcode = 6;
    rsp_bits.txn_id = 12'h202;
    rsp_bits.dbid_or_group_id = 12'h066;
    rsp_valid = 1;
    rdat_valid = 1;
    tick(); // Read retirement and write DBID acceptance coincide.
    rsp_valid = 0;
    rdat_valid = 0;
    wdat_bits.txn_id = 12'h066;
    wdat_bits.data_id = 1;
    wdat_valid = 1;
    tick();
    wdat_valid = 0;
    // Allocate into the freed read slot while accepting the last write DAT.
    req_bits.opcode = 7'h04;
    req_bits.txn_id = 12'h101;
    req_bits.return_txn_id_or_stash_lpid = 12'h101;
    req_bits.size_or_num_req = 4;
    req_valid = 1;
    wdat_bits.data_id = 0;
    wdat_valid = 1;
    tick();
    req_valid = 0;
    wdat_valid = 0;
    rsp_bits.opcode = 4;
    rsp_valid = 1;
    rdat_valid = 1;
    tick(); // Both transactions retire on different channels.
    rsp_valid = 0;
    rdat_valid = 0;
    request(0); // Retirement must permit identity reuse.
    read_response();
    if (MODE != 0) $fatal(1, "missing expected assertion");
    $display("CHI ready-valid requester/subordinate monitor simulation passed");
    $finish;
  end
endmodule

module chi_channel_monitor_tb;
  chi_channel_monitor_test test();
endmodule
module chi_channel_monitor_duplicate_tb;
  chi_channel_monitor_test #(.MODE(1)) test();
endmodule
module chi_channel_monitor_identity_tb;
  chi_channel_monitor_test #(.MODE(2)) test();
endmodule
module chi_channel_monitor_early_data_tb;
  chi_channel_monitor_test #(.MODE(3)) test();
endmodule
