// Checks speculative RAS updates, resolved recovery, wraparound, underflow, and coroutine replacement.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_ras_tb;
  typedef struct packed { logic [1:0] action; logic [63:0] return_address; } update_bits_t;
  typedef struct packed { logic valid; update_bits_t bits; } update_t;
  typedef struct packed { update_bits_t actual; logic [1:0] predicted_action; } resolution_bits_t;
  typedef struct packed { logic valid; resolution_bits_t bits; } resolution_t;
  typedef struct packed { logic valid; } pulse_t;
  localparam logic [1:0] NONE = 0, PUSH = 1, POP = 2, POP_PUSH = 3;
  logic clock = 0, reset = 1;
  logic [31:0] instruction = 0;
  logic [1:0] classification;
  update_t speculate_in = '0;
  resolution_t resolve_in = '0;
  pulse_t restore_in = '0, clear_in = '0;
  logic head_valid;
  logic [63:0] head;
  RV5StageRasFixture dut (.*);
  always #5 clock = ~clock;

  function automatic logic [31:0] jal(input logic [4:0] rd);
    return {20'd0, rd, 7'h6f};
  endfunction
  function automatic logic [31:0] jalr(input logic [4:0] rd, rs1);
    return {12'd0, rs1, 3'd0, rd, 7'h67};
  endfunction
  task automatic check_classification(input logic [31:0] encoded, input logic [1:0] expected);
    instruction = encoded;
    #1;
    assert (classification == expected)
      else $fatal(1, "RAS classification=%0d expected=%0d instruction=%h", classification, expected, instruction);
  endtask

  task automatic speculate(input logic [1:0] action, input logic [63:0] address = 0);
    @(negedge clock);
    speculate_in = '{valid: 1'b1, bits: '{action: action, return_address: address}};
    @(negedge clock);
    speculate_in = '0;
  endtask
  task automatic resolve(input logic [1:0] actual, predicted, input logic [63:0] address = 0);
    @(negedge clock);
    resolve_in = '{valid: 1'b1, bits: '{actual: '{action: actual, return_address: address}, predicted_action: predicted}};
    @(negedge clock);
    resolve_in = '0;
  endtask
  task automatic pulse_restore;
    @(negedge clock);
    restore_in.valid = 1;
    @(negedge clock);
    restore_in.valid = 0;
  endtask
  task automatic pulse_clear;
    @(negedge clock);
    clear_in.valid = 1;
    @(negedge clock);
    clear_in.valid = 0;
  endtask
  task automatic check(input bit valid, input logic [63:0] address = 0);
    #1;
    assert (head_valid == valid && (!valid || head == address))
      else $fatal(1, "RAS head valid=%b address=%h, expected valid=%b address=%h", head_valid, head, valid, address);
  endtask
  task automatic push_and_resolve(input logic [63:0] address);
    speculate(PUSH, address);
    resolve(PUSH, PUSH, address);
  endtask

  initial begin
    repeat (2) @(negedge clock);
    reset = 0;
    check(0);
    check_classification(jal(1), PUSH);
    check_classification(jal(5), PUSH);
    check_classification(jal(2), NONE);
    check_classification(jalr(1, 2), PUSH);
    check_classification(jalr(0, 1), POP);
    check_classification(jalr(0, 5), POP);
    check_classification(jalr(1, 1), PUSH);
    check_classification(jalr(5, 5), PUSH);
    check_classification(jalr(1, 5), POP_PUSH);
    check_classification(jalr(5, 1), POP_PUSH);
    check_classification(jalr(0, 2), NONE);

    speculate(PUSH, 64'h100);
    speculate(PUSH, 64'h200);
    check(1, 64'h200);
    resolve(PUSH, PUSH, 64'h100);
    resolve(PUSH, PUSH, 64'h200);
    pulse_restore();
    check(1, 64'h200);

    speculate(POP);
    check(1, 64'h100);
    resolve(POP, POP);
    check(1, 64'h100);
    speculate(PUSH, 64'h300);
    check(1, 64'h300);
    resolve(NONE, PUSH);
    check(1, 64'h100);

    speculate(PUSH, 64'h400);
    pulse_restore();
    check(1, 64'h100);
    speculate(POP_PUSH, 64'h500);
    resolve(POP_PUSH, POP_PUSH, 64'h500);
    check(1, 64'h500);

    pulse_clear();
    push_and_resolve(64'h10);
    push_and_resolve(64'h20);
    push_and_resolve(64'h30);
    push_and_resolve(64'h40);
    check(1, 64'h40);
    speculate(POP); check(1, 64'h30); resolve(POP, POP);
    speculate(POP); check(1, 64'h20); resolve(POP, POP);
    speculate(POP); resolve(POP, POP); check(0);
    speculate(POP); resolve(POP, POP); check(0);
    speculate(POP_PUSH, 64'h600); resolve(POP_PUSH, POP_PUSH, 64'h600); check(1, 64'h600);

    pulse_clear();
    check(0);
    $display("RV5Stage speculative and resolved RAS simulation passed");
    $finish;
  end
  initial begin #20000; $fatal(1, "RAS timeout"); end
endmodule
