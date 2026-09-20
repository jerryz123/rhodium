// Simulates RV32I integer ALU resource controls and shift-width edge cases.
// SPDX-License-Identifier: Apache-2.0
module rv32i_alu_tb;
  logic [31:0] left;
  logic [31:0] right;
  AluControl control;
  logic [31:0] result;

  ALU dut (.*);

  task automatic check_alu(input logic [2:0] result_select, input logic subtract, signed_compare, shift_right, arithmetic_shift, input logic [1:0] logic_select, input logic [31:0] left_value, right_value, expected);
    control = '0;
    control.result_select = result_select;
    control.subtract = subtract;
    control.signed_compare = signed_compare;
    control.shift_right = shift_right;
    control.arithmetic_shift = arithmetic_shift;
    control.logic_select = logic_select;
    left = left_value;
    right = right_value;
    #1;
    assert (result === expected) else $fatal(1, "ALU result %h, expected %h", result, expected);
  endtask

  initial begin
    check_alu(0, 0, 0, 0, 0, 0, 32'hffff_ffff, 32'd1, 32'd0);
    check_alu(0, 1, 0, 0, 0, 0, 32'd0, 32'd1, 32'hffff_ffff);
    check_alu(1, 0, 0, 0, 0, 0, 32'h1, 32'd31, 32'h8000_0000);
    check_alu(1, 0, 0, 0, 0, 0, 32'h1, 32'd32, 32'h1);
    check_alu(3, 1, 1, 0, 0, 0, 32'hffff_ffff, 32'd0, 32'd1);
    check_alu(3, 1, 1, 0, 0, 0, 32'h7fff_ffff, 32'h8000_0000, 32'd0);
    check_alu(3, 1, 0, 0, 0, 0, 32'hffff_ffff, 32'd0, 32'd0);
    check_alu(3, 1, 0, 0, 0, 0, 32'd0, 32'hffff_ffff, 32'd1);
    check_alu(2, 0, 0, 0, 0, 2, 32'haa55_aa55, 32'hffff_0000, 32'h55aa_aa55);
    check_alu(2, 0, 0, 0, 0, 1, 32'hf000, 32'h0f0f, 32'hff0f);
    check_alu(2, 0, 0, 0, 0, 0, 32'hf0f0, 32'h0ff0, 32'h00f0);
    check_alu(1, 0, 0, 1, 0, 0, 32'h8000_0000, 32'd31, 32'd1);
    check_alu(1, 0, 0, 1, 0, 0, 32'h8000_0000, 32'd32, 32'h8000_0000);
    check_alu(1, 0, 0, 1, 1, 0, 32'h8000_0000, 32'd31, 32'hffff_ffff);
    check_alu(1, 0, 0, 1, 1, 0, 32'h8000_0000, 32'd1, 32'hc000_0000);
    $display("RV32I integer ALU simulation passed");
    $finish;
  end
endmodule
