// Exercises every standard-B and Zicond resource family in the RV64 ALU.
// SPDX-License-Identifier: Apache-2.0
module bit_manip_tb;
  logic [63:0] left;
  logic [63:0] right;
  AluControl control;
  logic [63:0] result;

  ALU dut (.*);

  task automatic apply_and_check(input logic [63:0] left_value, right_value, expected);
    left = left_value;
    right = right_value;
    #1;
    assert (result === expected) else $fatal(1, "bit-manip result %h, expected %h", result, expected);
  endtask

  task automatic check_adder(input logic unsigned_word, input logic [1:0] shift_amount, input logic [63:0] left_value, right_value, expected);
    control = '0;
    control.result_select = 3'd0;
    control.unsigned_word = unsigned_word;
    control.shift_add_amount = shift_amount;
    apply_and_check(left_value, right_value, expected);
  endtask

  task automatic check_shift(input logic shift_right, extract_bit, unsigned_word, input logic [63:0] left_value, right_value, expected);
    control = '0;
    control.result_select = 3'd1;
    control.shift_right = shift_right;
    control.extract_bit = extract_bit;
    control.unsigned_word = unsigned_word;
    apply_and_check(left_value, right_value, expected);
  endtask

  task automatic check_logic(input logic [1:0] select, input logic invert_right, one_hot_right, input logic [63:0] left_value, right_value, expected);
    control = '0;
    control.result_select = 3'd2;
    control.logic_select = select;
    control.invert_right = invert_right;
    control.logic_right_select = one_hot_right ? 2'd1 : 2'd0;
    apply_and_check(left_value, right_value, expected);
  endtask

  task automatic check_conditional(input logic invert_right, input logic [63:0] left_value, right_value, expected);
    control = '0;
    control.result_select = 3'd2;
    control.logic_select = 2'd0;
    control.invert_right = invert_right;
    control.logic_right_select = 2'd2;
    apply_and_check(left_value, right_value, expected);
  endtask

  task automatic check_minmax(input logic signed_compare, maximum, input logic [63:0] left_value, right_value, expected);
    control = '0;
    control.result_select = 3'd4;
    control.subtract = 1'b1;
    control.signed_compare = signed_compare;
    control.maximum = maximum;
    apply_and_check(left_value, right_value, expected);
  endtask

  task automatic check_count(input logic [1:0] select, input logic word, input logic [63:0] left_value, expected);
    control = '0;
    control.result_select = 3'd5;
    control.count_select = select;
    control.word = word;
    apply_and_check(left_value, 0, expected);
  endtask

  task automatic check_rotate(input logic shift_right, word, input logic [63:0] left_value, right_value, expected);
    control = '0;
    control.result_select = 3'd6;
    control.shift_right = shift_right;
    control.word = word;
    apply_and_check(left_value, right_value, expected);
  endtask

  task automatic check_unary(input logic [2:0] select, input logic [63:0] left_value, expected);
    control = '0;
    control.result_select = 3'd7;
    control.unary_select = select;
    apply_and_check(left_value, 0, expected);
  endtask

  initial begin
    check_adder(0, 2, 64'd3, 64'd5, 64'd17);
    check_adder(1, 1, 64'hffff_ffff_8000_0001, 64'd2, 64'h0000_0001_0000_0004);
    check_shift(0, 0, 1, 64'hffff_ffff_8000_0001, 64'd4, 64'h0000_0008_0000_0010);
    check_logic(0, 1, 0, 64'hff00, 64'h0f0f, 64'hf000);
    check_logic(1, 1, 0, 64'hf0, 64'hff, 64'hffff_ffff_ffff_fff0);
    check_logic(2, 1, 0, 64'h55, 64'haa, 64'hffff_ffff_ffff_ff00);
    check_count(0, 0, 64'h10, 64'd59);
    check_count(1, 0, 64'h100, 64'd8);
    check_count(2, 0, 64'hf0f, 64'd8);
    check_count(0, 1, 64'hffff_ffff_0000_0010, 64'd27);
    check_count(1, 1, 64'hffff_ffff_0000_0000, 64'd32);
    check_count(2, 1, 64'hffff_ffff_0000_000f, 64'd4);
    check_minmax(1, 0, -64'd1, 64'd1, -64'd1);
    check_minmax(0, 1, -64'd1, 64'd1, -64'd1);
    check_unary(0, 64'h0100_0002_0000_0300, 64'hff00_00ff_0000_ff00);
    check_unary(1, 64'h0123_4567_89ab_cdef, 64'hefcd_ab89_6745_2301);
    check_rotate(0, 0, 64'h8000_0000_0000_0001, 1, 64'h0000_0000_0000_0003);
    check_rotate(1, 1, 64'h0000_0000_8000_0001, 1, 64'hffff_ffff_c000_0000);
    check_unary(2, 64'h80, 64'hffff_ffff_ffff_ff80);
    check_unary(3, 64'h8001, 64'hffff_ffff_ffff_8001);
    check_unary(4, 64'hffff_ffff_ffff_8001, 64'h8001);
    check_logic(0, 1, 1, 64'hff, 3, 64'hf7);
    check_shift(1, 1, 0, 64'h08, 3, 64'h1);
    check_logic(2, 0, 1, 64'h08, 3, 64'h0);
    check_logic(1, 0, 1, 64'h00, 63, 64'h8000_0000_0000_0000);
    check_conditional(0, 64'h0123_4567_89ab_cdef, 0, 0);
    check_conditional(0, 64'h0123_4567_89ab_cdef, 1, 64'h0123_4567_89ab_cdef);
    check_conditional(1, 64'h0123_4567_89ab_cdef, 0, 64'h0123_4567_89ab_cdef);
    check_conditional(1, 64'h0123_4567_89ab_cdef, 1, 0);
    $display("bit manipulation simulation passed");
    $finish;
  end
endmodule
