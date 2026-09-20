// Simulates every RV64I integer ALU resource-control combination and width edge case.
// SPDX-License-Identifier: Apache-2.0
module rv64i_alu_tb;
  logic [63:0] left;
  logic [63:0] right;
  AluControl control;
  logic [63:0] result;

  ALU dut (.*);

  task automatic check_adder(input logic subtract, input logic word, input logic [63:0] left_value, right_value, expected);
    control = '0;
    control.result_select = 3'd0;
    control.subtract = subtract;
    control.word = word;
    left = left_value;
    right = right_value;
    #1;
    assert (result === expected) else $fatal(1, "adder result %h, expected %h", result, expected);
  endtask

  task automatic check_shift(input logic shift_right, input logic arithmetic_shift, input logic word, input logic [63:0] left_value, right_value, expected);
    control = '0;
    control.result_select = 3'd1;
    control.shift_right = shift_right;
    control.arithmetic_shift = arithmetic_shift;
    control.word = word;
    left = left_value;
    right = right_value;
    #1;
    assert (result === expected) else $fatal(1, "shift result %h, expected %h", result, expected);
  endtask

  task automatic check_logic(input logic [1:0] select, input logic [63:0] left_value, right_value, expected);
    control = '0;
    control.result_select = 3'd2;
    control.logic_select = select;
    left = left_value;
    right = right_value;
    #1;
    assert (result === expected) else $fatal(1, "logic result %h, expected %h", result, expected);
  endtask

  task automatic check_compare(input logic signed_compare, input logic [63:0] left_value, right_value, expected);
    control = '0;
    control.result_select = 3'd3;
    control.subtract = 1'b1;
    control.signed_compare = signed_compare;
    left = left_value;
    right = right_value;
    #1;
    assert (result === expected) else $fatal(1, "compare result %h, expected %h", result, expected);
  endtask

  initial begin
    check_adder(0, 0, 64'hffff_ffff_ffff_ffff, 64'd1, 64'd0);
    check_adder(1, 0, 64'd0, 64'd1, 64'hffff_ffff_ffff_ffff);
    check_shift(0, 0, 0, 64'h1, 64'd63, 64'h8000_0000_0000_0000);
    check_shift(0, 0, 0, 64'h1, 64'd64, 64'h1);
    check_compare(1, 64'hffff_ffff_ffff_ffff, 64'd0, 64'd1);
    check_compare(1, 64'h7fff_ffff_ffff_ffff, 64'h8000_0000_0000_0000, 64'd0);
    check_compare(0, 64'hffff_ffff_ffff_ffff, 64'd0, 64'd0);
    check_compare(0, 64'd0, 64'hffff_ffff_ffff_ffff, 64'd1);
    check_logic(2, 64'haa55_aa55_aa55_aa55, 64'hffff_0000_ffff_0000, 64'h55aa_aa55_55aa_aa55);
    check_shift(1, 0, 0, 64'h8000_0000_0000_0000, 64'd63, 64'd1);
    check_shift(1, 0, 0, 64'h8000_0000_0000_0000, 64'd64, 64'h8000_0000_0000_0000);
    check_shift(1, 1, 0, 64'h8000_0000_0000_0000, 64'd63, 64'hffff_ffff_ffff_ffff);
    check_shift(1, 1, 0, 64'h8000_0000_0000_0000, 64'd1, 64'hc000_0000_0000_0000);
    check_logic(1, 64'hf000, 64'h0f0f, 64'hff0f);
    check_logic(0, 64'hf0f0, 64'h0ff0, 64'h00f0);
    check_adder(0, 1, 64'h0000_0000_7fff_ffff, 64'd1, 64'hffff_ffff_8000_0000);
    check_adder(0, 1, 64'hffff_ffff_ffff_ffff, 64'd1, 64'd0);
    check_adder(1, 1, 64'd0, 64'd1, 64'hffff_ffff_ffff_ffff);
    check_shift(0, 0, 1, 64'd1, 64'd31, 64'hffff_ffff_8000_0000);
    check_shift(0, 0, 1, 64'd1, 64'd32, 64'd1);
    check_shift(1, 0, 1, 64'h0000_0000_8000_0000, 64'd31, 64'd1);
    check_shift(1, 1, 1, 64'h0000_0000_8000_0000, 64'd31, 64'hffff_ffff_ffff_ffff);
    check_shift(1, 1, 1, 64'h0000_0000_8000_0000, 64'd1, 64'hffff_ffff_c000_0000);
    $display("RV64I integer ALU simulation passed");
    $finish;
  end
endmodule
