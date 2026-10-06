// SPDX-License-Identifier: Apache-2.0
module chi_inclusive_directory_tb;
  logic clock = 0, reset = 1;
  logic lookup_valid = 0;
  logic [1:0] lookup_set = 0;
  logic [2:0] commit_valid = 0, commit_ways = 0;
  logic [5:0] commit_sets = 0;
  logic [47:0] commit_entries = 0;
  wire initialized, lookup_accepted, response_valid;
  wire [2:0] commit_accepted;
  wire [31:0] response;
  CHIDirectoryFixture dut(.*);
  logic [15:0] memory [4][2];
  logic [31:0] expected_response;
  bit expected_valid = 0, swept = 0, prefer_commit = 0;
  int sweep_set = 0, priority_slot = 0;
  int grants [3] = '{default:0};
  int reads = 0;
  int winner;
  bit write_op, read_op;
  logic [2:0] expected_grant;

  task automatic step;
    #1;
    winner = -1;
    for (int offset = 0; offset < 3; offset++)
      if (winner < 0 && commit_valid[(priority_slot + offset) % 3]) winner = (priority_slot + offset) % 3;
    write_op = swept && !reset && winner >= 0 && (!lookup_valid || prefer_commit);
    read_op = swept && !reset && lookup_valid && !write_op;
    expected_grant = write_op ? 3'(1 << winner) : 0;
    assert(initialized == (swept && !reset) && lookup_accepted == read_op && commit_accepted == expected_grant)
      else $fatal(1, "directory arbitration or sweep exclusion mismatch");
    assert(response_valid == (expected_valid && swept && !reset))
      else $fatal(1, "directory response latency mismatch");
    if (response_valid) assert(response == expected_response)
      else $fatal(1, "directory row mismatch: %h expected %h", response, expected_response);
    if (reset) begin
      swept = 0; sweep_set = 0; priority_slot = 0; prefer_commit = 0; expected_valid = 0;
    end else if (!swept) begin
      memory[sweep_set][0] = 0; memory[sweep_set][1] = 0;
      if (sweep_set == 3) swept = 1; else sweep_set++;
      expected_valid = 0;
    end else begin
      expected_valid = read_op;
      if (read_op) begin
        expected_response = {memory[lookup_set][1], memory[lookup_set][0]};
        prefer_commit = 1; reads++;
      end
      if (write_op) begin
        memory[commit_sets[2*winner +: 2]][commit_ways[winner]] = commit_entries[16*winner +: 16];
        priority_slot = (winner + 1) % 3; prefer_commit = 0; grants[winner]++;
      end
    end
    clock = 1; #1; clock = 0; #1;
  endtask

  initial begin
    step(); reset = 0;
    commit_valid = 7; lookup_valid = 1;
    step(); step(); reset = 1; step(); reset = 0;
    repeat (4) step();
    // Keep all frozen candidates eligible: exactly 30 reads and ten commits
    // per slot under persistent two-class contention, including slot 2 wrap.
    commit_sets = {2'd2, 2'd1, 2'd0};
    commit_ways = 3'b010;
    commit_entries = 48'h9abc_5678_1234;
    for (int i = 0; i < 60; i++) begin lookup_set = 2'(i); step(); end
    assert(reads == 30 && grants[0] == 10 && grants[1] == 10 && grants[2] == 10)
      else $fatal(1, "directory did not fairly serve persistent candidates");
    // Vary both eligible classes and lane payloads deterministically.
    for (int i = 0; i < 100; i++) begin
      commit_valid = 3'(i); lookup_valid = (i % 3 != 0); lookup_set = 2'(i / 3);
      commit_sets = 6'(i); commit_ways = 3'(i / 7); commit_entries = 48'h123456789abc ^ 48'(i * 65539);
      step();
    end
    commit_valid = 0; lookup_valid = 1;
    for (int i = 0; i < 4; i++) begin lookup_set = 2'(i); step(); end
    // Reset with a read response outstanding and commits waiting, then prove
    // every lane is cold; no SRAM initialization attribute is assumed.
    commit_valid = 7; reset = 1; step(); reset = 0;
    repeat (4) step();
    commit_valid = 0;
    for (int i = 0; i < 4; i++) begin lookup_set = 2'(i); step(); end
    lookup_valid = 0; step();
    $display("inclusive directory SRAM passed"); $finish;
  end
endmodule
