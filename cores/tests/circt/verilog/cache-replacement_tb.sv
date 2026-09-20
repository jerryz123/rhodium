// Verifies tree-PLRU ordering, invalid-way priority, and padded-leaf exclusion.
// SPDX-License-Identifier: Apache-2.0
module cache_replacement_tb;
  logic clock = 0;
  logic reset = 1;
  logic [3:0] valid_four;
  logic touch_four_valid;
  logic [1:0] touch_four_way;
  logic [1:0] victim_four;
  logic [2:0] state_four;
  logic [2:0] valid_three;
  logic touch_three_valid;
  logic [1:0] touch_three_way;
  logic [1:0] victim_three;
  logic [2:0] state_three;

  CacheReplacementFixture dut (.*);
  always #5 clock = ~clock;

  task automatic tick;
    begin
      @(posedge clock);
      #1;
    end
  endtask

  task automatic touch_four(input logic [1:0] way);
    begin
      touch_four_way = way;
      touch_four_valid = 1;
      tick();
      touch_four_valid = 0;
    end
  endtask

  task automatic touch_three(input logic [1:0] way);
    begin
      touch_three_way = way;
      touch_three_valid = 1;
      tick();
      touch_three_valid = 0;
    end
  endtask

  initial begin
    valid_four = 4'b1111;
    touch_four_valid = 0;
    touch_four_way = 0;
    valid_three = 3'b111;
    touch_three_valid = 0;
    touch_three_way = 0;
    repeat (2) tick();
    reset = 0;

    assert (victim_four == 0 && victim_three == 0)
      else $fatal(1, "reset PLRU victim was not way zero");

    touch_four(0);
    assert (victim_four == 2) else $fatal(1, "four-way PLRU did not avoid way zero");
    touch_four(2);
    assert (victim_four == 1) else $fatal(1, "four-way PLRU did not select way one");
    touch_four(1);
    assert (victim_four == 3) else $fatal(1, "four-way PLRU did not select way three");
    touch_four(3);
    assert (victim_four == 0) else $fatal(1, "four-way PLRU did not return to way zero");

    valid_four = 4'b1010;
    #1;
    assert (victim_four == 0) else $fatal(1, "lowest invalid way did not override PLRU");
    valid_four = 4'b1011;
    #1;
    assert (victim_four == 2) else $fatal(1, "invalid way two did not override PLRU");
    valid_four = 4'b1111;

    touch_three(0);
    assert (victim_three == 2) else $fatal(1, "three-way PLRU did not select way two");
    touch_three(2);
    assert (victim_three == 1) else $fatal(1, "three-way PLRU did not select way one");
    touch_three(1);
    assert (victim_three == 2) else $fatal(1, "three-way PLRU selected a padded leaf");
    assert (victim_three < 3) else $fatal(1, "padded PLRU leaf became eligible");

    $display("Reusable tree-PLRU checks passed");
    $finish;
  end
endmodule
