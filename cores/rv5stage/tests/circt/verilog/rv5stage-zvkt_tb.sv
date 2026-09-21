// Compares vector timing under identical control and distinct data for every implemented Zvkt form.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_zvkt_tb;
  localparam int VLEN = 128, AW = $clog2(32 * VLEN / 64);
  typedef struct packed { logic [AW-1:0] address; logic [63:0] data, mask; } write_t;
  typedef struct packed { logic valid; write_t bits; } write_port_t;
  logic clock = 0, reset = 1;
  logic [8:0] probe;
  logic [63:0] vtype, vl, vstart, left_scalar, right_scalar, left_floating_scalar, right_floating_scalar;
  logic [1:0] vxrm;
  logic request_valid, issue_ready, cancel;
  write_port_t left_initialize_in, right_initialize_in;
  logic active, request_ready, legal, issued, committed, data_differed;
  logic [15:0] probe_count;
  logic control_vs1, control_rs1, sew64, v0_data;
  int cycles, commits, differing_results;
  logic [63:0] rng = 64'hd1b54a32d192ed03;
  RV5StageZvkt dut (.*);
  always #5 clock = ~clock;

  function automatic logic [63:0] random_word();
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return rng;
  endfunction

  task automatic tick;
    @(negedge clock); #1;
    cycles++;
    if (committed) commits++;
    if (data_differed) differing_results++;
  endtask

  task automatic initialize_banks;
    logic [63:0] left_data, right_data;
    for (int address = 0; address < 32 * VLEN / 64; address++) begin
      left_data = 64'h0101010101010101 * 64'(address + 1);
      right_data = random_word();
      // Ordinary v0 masks are control; carry/merge consume v0 as data instead.
      if (address < VLEN / 64 || (control_vs1 && address >= 16 * VLEN / 64 && address < 17 * VLEN / 64)) begin
        left_data = address < VLEN / 64 ? 64'hd6b59a6cc3a55aa5 : 64'h0001000000030002;
        right_data = address < VLEN / 64 && v0_data ? ~left_data : left_data;
      end
      left_initialize_in = '{1'b1, '{AW'(address), left_data, 64'hffffffffffffffff}};
      right_initialize_in = '{1'b1, '{AW'(address), right_data, 64'hffffffffffffffff}};
      tick();
    end
    left_initialize_in.valid = 0;
    right_initialize_in.valid = 0;
  endtask

  task automatic run_probe(input int index, start);
    int elapsed, prior_commits;
    probe = 9'(index);
    #1;
    vtype = 64'((sew64 ? 3 : 2) << 3);
    vl = sew64 ? 2 : 3;
    vstart = 64'(start);
    left_scalar = control_rs1 ? 3 : 64'h0123456789abcdef;
    right_scalar = control_rs1 ? 3 : 64'hfedcba9876543210;
    left_floating_scalar = 64'hffffffff3f800000;
    right_floating_scalar = 64'hffffffffc0200000;
    vxrm = 2'(index);
    request_valid = 0; issue_ready = 1; cancel = 0;
    reset = 1; repeat (2) tick(); reset = 0;
    initialize_banks();
    assert (legal) else $fatal(1, "illegal generated Zvkt probe %0d", index);
    prior_commits = commits;
    request_valid = 1;
    while (!request_ready) tick();
    tick();
    request_valid = 0;
    for (elapsed = 0; elapsed < 500 && (active || commits == prior_commits); elapsed++) begin
      issue_ready = (elapsed + index) % 5 != 0;
      tick();
    end
    issue_ready = 1;
    assert (!active && commits > prior_commits)
      else $fatal(1, "Zvkt probe did not complete: probe=%0d start=%0d", index, start);
  endtask

  initial begin
    probe = 0; vtype = 0; vl = 0; vstart = 0; left_scalar = 0; right_scalar = 0; left_floating_scalar = 0; right_floating_scalar = 0; vxrm = 0;
    request_valid = 0; issue_ready = 0; cancel = 0;
    left_initialize_in = '0; right_initialize_in = '0;
    repeat (3) tick();
    for (int index = 0; index < int'(probe_count); index++) begin
      run_probe(index, 0);
      run_probe(index, 1);
    end
    assert (differing_results > 0) else $fatal(1, "vacuous Zvkt comparison");
    $display("Zvkt timing PASS: %0d probes, %0d commits, %0d cycles", probe_count, commits, cycles);
    $finish;
  end
endmodule
