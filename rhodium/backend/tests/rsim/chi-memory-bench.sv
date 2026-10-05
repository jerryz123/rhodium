// SPDX-License-Identifier: Apache-2.0
`include "chi-types.svh"
module CHIMemoryBench;
  bit clock = 0, reset = 1;
  bit req_valid = 0, dat_valid = 0, rsp_ready = 0, read_ready = 0;
  test_req_t req_bits = '0;
  test_dat_t dat_bits = '0;
  wire req_ready, dat_ready, rsp_valid, read_valid;
  test_rsp_t rsp_bits;
  test_dat_t read_bits;
  RsimCHIMemory dut(.*);
  import "DPI-C" context function int rsim_chi_test_registry(input int phase);
  always #5 clock = ~clock;
  int cycle = 0;
  bit launched_req = 0, launched_dat = 0, launched_rsp_ready = 0, launched_read_ready = 0;
  // Trace only defined payloads and actual memory-side transfers, including
  // every cycle of stalls. Launch registers are testbench state, not DUT latency.
  always @(posedge clock) begin
    if (!reset) begin
      $display("CYCLE %0d %b %b %b %b %b %b", cycle, req_ready, dat_ready,
               launched_req && req_ready, launched_dat && dat_ready, rsp_valid, read_valid);
      if (rsp_valid) $display("RSP %0d %b %h", cycle, launched_rsp_ready, rsp_bits);
      if (read_valid) $display("DAT %0d %b %h", cycle, launched_read_ready, read_bits);
    end
    launched_req <= reset ? 0 : req_valid;
    launched_dat <= reset ? 0 : dat_valid;
    launched_rsp_ready <= reset ? 0 : rsp_ready;
    launched_read_ready <= reset ? 0 : read_ready;
    cycle <= cycle + 1;
  end
  task automatic tick;
    @(posedge clock); #1;
  endtask
  task automatic request(input logic [6:0] opcode, input logic [11:0] id, input logic [43:0] address);
    req_bits = '0;
    req_bits.opcode = opcode; req_bits.txn_id = id;
    req_bits.src_id = 3; req_bits.tgt_id = 9;
    req_bits.address = address; req_bits.size_or_num_req = 6;
    req_bits.return_nid_or_stash_nid_or_data_target = 3;
    req_bits.return_txn_id_or_stash_lpid = id;
    req_valid = 1; tick();
    for (int n = 0; !req_ready && n < 80; n++) tick();
    if (!req_ready) $fatal(1, "request timeout");
    req_valid = 0; tick(); req_bits = '0;
  endtask
  task automatic response(input logic [4:0] opcode, input logic [11:0] id, output logic [11:0] dbid);
    test_rsp_t held;
    for (int n = 0; !rsp_valid && n < 80; n++) tick();
    if (!rsp_valid || rsp_bits.opcode != opcode || rsp_bits.txn_id != id ||
        rsp_bits.src_id != 9 || rsp_bits.tgt_id != 3 || rsp_bits.resp_err != 0)
      $fatal(1, "response mismatch");
    dbid = rsp_bits.dbid_or_group_id;
    held = rsp_bits;
    repeat (4) begin tick(); if (!rsp_valid || rsp_bits != held) $fatal(1, "RSP stall"); end
    rsp_ready = 1; tick(); rsp_ready = 0; tick();
  endtask
  task automatic write_data(input logic [11:0] dbid, input logic [63:0] mask, input logic [511:0] data);
    dat_bits = '0;
    dat_bits.opcode = 3; dat_bits.src_id = 3; dat_bits.tgt_id = 9;
    dat_bits.txn_id = dbid; dat_bits.data_id = 0;
    dat_bits.byte_enable = mask; dat_bits.data = data;
    dat_valid = 1; tick();
    for (int n = 0; !dat_ready && n < 80; n++) tick();
    if (!dat_ready) $fatal(1, "write data timeout");
    dat_valid = 0; tick(); dat_bits = '0;
  endtask
  task automatic read_data(input logic [11:0] id, input logic [511:0] data);
    test_dat_t held;
    for (int n = 0; !read_valid && n < 80; n++) tick();
    if (!read_valid || read_bits.opcode != 4 || read_bits.txn_id != id ||
        read_bits.src_id != 9 || read_bits.tgt_id != 3 || read_bits.data_id != 0 ||
        read_bits.byte_enable != '1 || read_bits.data != data || read_bits.resp_err != 0)
      $fatal(1, "read data mismatch: %h expected %h", read_bits.data, data);
    held = read_bits;
    repeat (5) begin tick(); if (!read_valid || read_bits != held) $fatal(1, "DAT stall"); end
    read_ready = 1; tick(); read_ready = 0; tick();
  endtask
  logic [511:0] initial_data, full_data, partial_data, expected;
  logic [11:0] dbid, ignored;
  localparam logic [63:0] MASK = 64'h8000000180000081;
  initial begin
    for (int i = 0; i < 64; i++) begin
      initial_data[i*8 +: 8] = 8'(i ^ 'h5a);
      full_data[i*8 +: 8] = 8'(i + 1);
      partial_data[i*8 +: 8] = 8'('ha0 + i);
      expected[i*8 +: 8] = MASK[i] ? partial_data[i*8 +: 8] : full_data[i*8 +: 8];
    end
    repeat (3) tick();
    if (rsim_chi_test_registry(0) != 0) $fatal(1, "reset registration");
    reset = 0;
    request(7'h04, 12'h100, 44'h80000000); read_data(12'h100, initial_data);
    request(7'h1d, 12'h101, 44'h80000100); response(6, 12'h101, dbid);
    write_data(dbid, '1, full_data); response(4, 12'h101, ignored);
    if (ignored != dbid) $fatal(1, "completion DBID");
    request(7'h1c, 12'h102, 44'h80000100); response(6, 12'h102, dbid);
    write_data(dbid, MASK, partial_data); response(4, 12'h102, ignored);
    request(7'h04, 12'h103, 44'h80000100); read_data(12'h103, expected);
    // Exhaust transaction slots while DAT is stalled. A third REQ must wait.
    request(7'h04, 12'h104, 44'h80000100);
    request(7'h04, 12'h105, 44'h80000000);
    repeat (8) tick();
    // A rejected opcode is consumed independently of occupancy. Present a
    // supported idle payload before checking admission for another read.
    req_bits.opcode = 7'h04; tick();
    if (req_ready) $fatal(1, "outstanding slots did not backpressure REQ");
    read_data(12'h104, expected); read_data(12'h105, initial_data);
    // Reset cancels an incomplete write without clearing the native byte store.
    request(7'h1c, 12'h106, 44'h80000100); response(6, 12'h106, dbid);
    reset = 1; repeat (3) tick(); reset = 0;
    if (rsp_valid || read_valid) $fatal(1, "reset did not clear responses");
    if (rsim_chi_test_registry(1) != 0) $fatal(1, "reset changed registration or storage");
    request(7'h04, 12'h107, 44'h80000100); read_data(12'h107, expected);
    repeat (5) tick();
    if (rsp_valid || read_valid) $fatal(1, "duplicate completion");
    $display("CHI_MEMORY_PASS"); $finish;
  end
  initial begin #20000; $fatal(1, "CHI timeout"); end
endmodule
