// Checks associative PC/halfword selection, saturating counters, replacement, and invalidation.
module rv5stage_btb_tb;
  typedef struct packed { logic valid; logic [63:0] pc, target; logic compressed; } prediction_t;
  typedef struct packed { logic [63:0] pc, target; logic branch, conditional, taken, compressed; } update_bits_t;
  typedef struct packed { logic valid; update_bits_t bits; } update_t;
  typedef struct packed { logic valid; logic [63:0] bits; } invalidate_t;
  logic clock = 0, reset = 1, invalidate_all = 0;
  logic [63:0] cursor = 0;
  prediction_t prediction;
  update_t update_in = '0;
  invalidate_t invalidate_in = '0;
  RV5StageBtb dut (.*);
  always #5 clock = ~clock;

  task automatic train(input logic [63:0] pc, target, input bit conditional, taken, compressed = 0, branch = 1);
    @(negedge clock);
    update_in = '{1'b1, '{pc, target, branch, conditional, taken, compressed}};
    @(negedge clock);
    update_in = '0;
  endtask
  task automatic check(input logic [63:0] pc, input bit valid, input logic [63:0] target = 0);
    cursor = pc;
    #1;
    assert (prediction.valid == valid && (!valid || prediction.target == target))
      else $fatal(1, "BTB query %h valid=%b target=%h", pc, prediction.valid, prediction.target);
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
    update_in = '{1'b1, '{64'h108, 64'h700, 1'b1, 1'b0, 1'b1, 1'b0}};
    @(negedge clock);
    invalidate_in = '0;
    update_in = '0;
    check('h108, 0);
    invalidate_all = 1;
    @(negedge clock);
    invalidate_all = 0;
    check('h104, 0);
    train('h102, 'h300, 0, 1, 1);
    train('h100, 'h200, 0, 1, 1);
    check('h100, 1, 'h200); // Address order wins even when allocated later.
    check('h102, 1, 'h300);
    invalidate_all = 1;
    update_in = '{1'b1, '{64'h104, 64'h500, 1'b1, 1'b0, 1'b1, 1'b0}};
    @(negedge clock);
    invalidate_all = 0;
    update_in = '0;
    check('h104, 0);
    $display("RV5Stage associative BTB counters and replacement passed");
    $finish;
  end
  initial begin #20000; $fatal(1, "BTB timeout"); end
endmodule
