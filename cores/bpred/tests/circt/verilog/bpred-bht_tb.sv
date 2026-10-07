// Checks direction counter saturation, bank independence, current-state training, and lazy clear/reset.
// SPDX-License-Identifier: Apache-2.0
module bpred_bht_tb;
  logic clock=0, reset=1, clear_in=0;
  logic [2:0] row=0, prefix_row=0;
  logic [3:0][1:0] counters;
  logic [1:0] prefix_counter;
  typedef struct packed { logic valid; logic [4:0] index; logic taken; } update_t;
  update_t update_in='0;
  int model[8][4];
  Bht dut(.*);
  always #5 clock=~clock;
  task automatic check_rows;
    for(int r=0;r<8;r++) begin
      row=3'(r); prefix_row=3'((r+3)%8); #1;
      for(int b=0;b<4;b++) assert(counters[b]==2'(model[r][b])) else $fatal(1,"row/bank mismatch %0d/%0d",r,b);
      assert(prefix_counter==2'(model[(r+3)%8][3])) else $fatal(1,"prefix row mismatch");
    end
  endtask
  initial begin
    for(int r=0;r<8;r++) for(int b=0;b<4;b++) model[r][b]=1;
    repeat(3) @(negedge clock); reset=0;
    check_rows();
    for(int pass=0;pass<10;pass++) for(int r=0;r<8;r++) for(int b=0;b<4;b++) begin
      @(negedge clock); update_in='{1'b1,5'(r*4+b),pass<5};
      @(negedge clock); update_in='0;
      model[r][b]=pass<5 ? (model[r][b]<3?model[r][b]+1:3) : (model[r][b]>0?model[r][b]-1:0);
      check_rows();
    end
    // Two adjacent updates alias the same saved index, with no stale lookup
    // counter supplied by the caller: both must increment the current state.
    @(negedge clock); update_in='{1'b1,5'd7,1'b1};
    repeat(2) @(negedge clock); update_in='0;
    model[1][3]=2; check_rows();
    @(negedge clock); clear_in=1; update_in='{1'b1,5'd7,1'b1};
    #1; assert(counters==8'h55 && prefix_counter==1) else $fatal(1,"clear lookup priority");
    @(negedge clock); clear_in=0; update_in='0;
    for(int r=0;r<8;r++) for(int b=0;b<4;b++) model[r][b]=1;
    check_rows();
    @(negedge clock); update_in='{1'b1,5'd7,1'b1};
    @(negedge clock); update_in='0; reset=1;
    @(negedge clock); reset=0; check_rows();
    $display("banked BHT counter simulation passed"); $finish;
  end
endmodule
