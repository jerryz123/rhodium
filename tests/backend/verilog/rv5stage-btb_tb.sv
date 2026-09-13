// Checks associative PC/halfword selection, saturating counters, replacement, and invalidation.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_btb_tb;
  typedef struct packed { logic valid; logic [63:0] pc, target; logic compressed; logic [1:0] ras_action; } prediction_t;
  typedef struct packed {
    logic [63:0] pc, target;
    logic branch, conditional, taken, compressed;
    logic [1:0] ras_action, predicted_ras_action;
    logic [63:0] return_address;
  } update_bits_t;
  typedef struct packed { logic valid; update_bits_t bits; } update_t;
  typedef struct packed { logic valid; prediction_t bits; } discovery_t;
  typedef struct packed { logic valid; logic [63:0] bits; } invalidate_t;
  typedef struct packed { logic valid; } pulse_t;
  logic clock = 0, reset = 1;
  logic [63:0] cursor = 0;
  prediction_t prediction;
  pulse_t invalidate_all_in = '0;
  update_t update_in = '0;
  discovery_t discover_in = '0;
  invalidate_t invalidate_in = '0;
  RV5StageBtb dut (.*);
  always #5 clock = ~clock;

  task automatic train(input logic [63:0] pc, target, input bit conditional, taken, compressed = 0, branch = 1, input logic [1:0] ras_action = 0);
    @(negedge clock);
    update_in = '{1'b1, '{pc, target, branch, conditional, taken, compressed, ras_action, ras_action, pc + (compressed ? 2 : 4)}};
    @(negedge clock);
    update_in = '0;
  endtask
  task automatic check(input logic [63:0] pc, input bit valid, input logic [63:0] target = 0, input logic [1:0] ras_action = 0);
    cursor = pc;
    #1;
    assert (prediction.valid == valid && (!valid || (prediction.target == target && prediction.ras_action == ras_action)))
      else $fatal(1, "BTB query %h valid=%b target=%h", pc, prediction.valid, prediction.target);
  endtask
  task automatic discover(input logic [63:0] pc, target, input bit compressed, input logic [1:0] ras_action);
    @(negedge clock);
    discover_in = '{1'b1, '{1'b1, pc, target, compressed, ras_action}};
    @(negedge clock);
    discover_in = '0;
  endtask
  initial begin
    repeat (2) @(negedge clock);
    reset = 0;
    check('h100, 0);
    train('h100, 'h200, 1, 0); // Not-taken misses do not allocate.
    check('h100, 0);
    train('h100, 'h200, 1, 1, 1); // Weak taken.
    check('h100, 1, 'h200);
    train('h100, 'h200, 1, 0, 1);
    check('h100, 0);
    repeat (3) train('h100, 'h200, 1, 0, 1);
    train('h100, 'h200, 1, 1, 1);
    check('h100, 0); // Saturation at zero requires two taken outcomes.
    train('h100, 'h200, 1, 1, 1);
    repeat (3) train('h100, 'h200, 1, 1, 1);
    train('h100, 'h200, 1, 0, 1);
    check('h100, 1, 'h200);
    train('h102, 'h302, 0, 1, 1);
    check('h100, 1, 'h200); // Earlier taken branch wins, not entry allocation order.
    check('h102, 1, 'h302); // Skip the lower branch on a halfword entry.
    train('h100, 'h200, 1, 0, 1);
    check('h100, 1, 'h302); // Lower not taken does not hide the upper jump.
    check('h100000100, 0); // Full address tags.
    train('h102, 'h402, 0, 1, 1);
    check('h102, 1, 'h402);
    train('h104, 'h500, 0, 1);
    train('h108, 'h600, 0, 1); // Fourth PC replaces entry zero.
    check('h100, 1, 'h402);
    check('h108, 1, 'h600);
    train('h102, 0, 0, 0, 0, 0); // Resolved nonbranch removes a stale entry.
    check('h102, 0);
    @(negedge clock);
    invalidate_in = '{1'b1, 64'h108};
    update_in = '{1'b1, '{64'h108, 64'h700, 1'b1, 1'b0, 1'b1, 1'b0, 2'd0, 2'd0, 64'h10c}};
    @(negedge clock);
    invalidate_in = '0;
    update_in = '0;
    check('h108, 0);
    invalidate_all_in.valid = 1;
    check('h104, 0); // Pulse suppresses lookup during the clear event.
    @(negedge clock);
    invalidate_all_in.valid = 0;
    check('h104, 0);
    train('h102, 'h300, 0, 1, 1);
    train('h100, 'h200, 0, 1, 1);
    check('h100, 1, 'h200); // Address order wins even when allocated later.
    check('h102, 1, 'h300);
    train('h10c, 'h700, 0, 1, 0, 1, 2'd2);
    check('h10c, 1, 'h700, 2'd2);
    discover('h110, 'h900, 0, 2'd2);
    check('h110, 1, 'h900, 2'd2);
    @(negedge clock);
    discover_in = '{1'b1, '{1'b1, 64'h114, 64'ha00, 1'b0, 2'd2}};
    update_in = '{1'b1, '{64'h118, 64'hb00, 1'b1, 1'b0, 1'b1, 1'b0, 2'd0, 2'd0, 64'h11c}};
    @(negedge clock);
    discover_in = '0;
    update_in = '0;
    check('h118, 1, 'hb00);
    check('h114, 0); // Resolved training wins over simultaneous discovery.
    invalidate_all_in.valid = 1;
    update_in = '{1'b1, '{64'h104, 64'h500, 1'b1, 1'b0, 1'b1, 1'b0, 2'd0, 2'd0, 64'h108}};
    @(negedge clock);
    invalidate_all_in.valid = 0;
    update_in = '0;
    check('h104, 0);
    $display("RV5Stage associative BTB counters and replacement passed");
    $finish;
  end
  initial begin #20000; $fatal(1, "BTB timeout"); end
endmodule
