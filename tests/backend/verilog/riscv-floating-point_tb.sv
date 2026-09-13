// Verifies reusable RISC-V NaN, rounding, move, classification, and flag policy.
// SPDX-License-Identifier: Apache-2.0
module riscv_floating_point_tb;
  logic [31:0] value;
  logic [31:0] other;
  logic [63:0] container;
  logic [63:0] integer_value;
  logic [2:0] instruction_mode;
  logic [2:0] dynamic_mode;
  logic [1:0] sign_operation;
  logic invalid;
  logic infinite;
  logic overflow;
  logic underflow;
  logic inexact;
  logic [63:0] boxed;
  logic box_valid;
  logic [31:0] unboxed;
  logic [31:0] canonical_nan;
  logic [31:0] canonicalized;
  logic rounding_valid;
  logic [2:0] rounding_mode;
  logic [4:0] flags;
  logic [4:0] integer_flags;
  logic [31:0] sign_result;
  logic [63:0] integer_move;
  logic [31:0] float_move;
  logic [9:0] classification;

  RiscvFloatingPointFixture dut (.*);

  initial begin
    value = 32'h3f800000;
    other = 32'hc0000000;
    container = 64'hffffffff3f800000;
    integer_value = 64'h0123456789abcdef;
    instruction_mode = 3'd7;
    dynamic_mode = 3'd3;
    sign_operation = 2'd0;
    invalid = 1'b1;
    infinite = 1'b1;
    overflow = 1'b1;
    underflow = 1'b1;
    inexact = 1'b1;
    #1;
    assert (boxed == 64'hffffffff3f800000 && box_valid && unboxed == 32'h3f800000);
    assert (canonical_nan == 32'h7fc00000 && canonicalized == value);
    assert (rounding_valid && rounding_mode == 3'd3);
    assert (flags == 5'b11111 && integer_flags == 5'b10001);
    assert (sign_result == 32'hbf800000);
    assert (integer_move == 64'h000000003f800000 && float_move == 32'h89abcdef);
    assert (classification == 10'b0001000000);

    container = 64'h000000003f800000;
    instruction_mode = 3'd7;
    dynamic_mode = 3'd5;
    value = 32'h7fa00001;
    other = 32'h40000000;
    #1;
    assert (!box_valid && unboxed == 32'h7fc00000);
    assert (canonicalized == 32'h7fc00000);
    assert (!rounding_valid);
    assert (classification == 10'b0100000000);

    $display("RISC-V floating-point helpers passed");
    $finish;
  end
endmodule
