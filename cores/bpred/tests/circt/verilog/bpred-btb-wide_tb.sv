// Checks address-ordered prediction at every halfword cursor of an eight-byte fetch block.
// SPDX-License-Identifier: Apache-2.0
module bpred_btb_wide_tb;
  typedef struct packed { logic valid; logic [63:0] pc, target; logic compressed; logic [1:0] ras_action; } prediction_t;
  typedef struct packed { logic [63:0] pc, target; logic branch, conditional, taken, compressed; logic [1:0] ras_action, predicted_ras_action; logic [63:0] return_address; } update_bits_t;
  typedef struct packed { logic valid; update_bits_t bits; } update_t;
  typedef struct packed { logic valid; prediction_t bits; } discovery_t;
  typedef struct packed { logic valid; logic [63:0] bits; } invalidate_t;
  typedef struct packed { logic valid; } pulse_t;
  logic clock=0, reset=1;
  logic [63:0] cursor=0;
  prediction_t prediction;
  pulse_t invalidate_all_in='0;
  update_t update_in='0;
  discovery_t discover_in='0;
  invalidate_t invalidate_in='0;
  Btb dut(.*);
  always #5 clock=~clock;
  task automatic train(int pc, bit conditional=0, bit taken=1);
    @(negedge clock);
    update_in='{1'b1, '{64'(pc),64'(pc+256),1'b1,conditional,taken,1'b1,2'd0,2'd0,64'(pc+2)}};
    @(negedge clock); update_in='0;
  endtask
  task automatic check(logic [63:0] pc, longint selected);
    cursor=64'(pc); #1;
    assert(prediction.valid==(selected>=0) && (selected<0 || (prediction.pc==64'(selected) && prediction.target==64'(selected+256))))
      else $fatal(1,"wide BTB cursor=%h predicted=%h wanted=%h",cursor,prediction.pc,selected);
  endtask
  initial begin
    repeat(2) @(negedge clock); reset=0;
    // Reverse allocation order must not determine selection.
    train('h106); train('h104); train('h102); train('h100);
    for(int offset=0;offset<8;offset+=2) check(64'h100+64'(offset),64'h100+64'(offset));
    check('h108,-1); check(64'h100000100,-1);
    @(negedge clock); invalidate_in='{1'b1,64'h102};
    @(negedge clock); invalidate_in='0;
    check('h102,'h104);
    train('h100,1,0); check('h100,'h104);
    train('h100,1,1); check('h100,'h100);
    train('h10e); check('h108,'h10e); check('h10e,'h10e); check('h110,-1);
    invalidate_all_in.valid=1; check('h100,-1);
    @(negedge clock); invalidate_all_in.valid=0; check('h106,-1);
    $display("Eight-byte BTB halfword ordering passed"); $finish;
  end
  initial begin #10000; $fatal(1,"wide BTB timeout"); end
endmodule
