// Verifies every RV5Stage AMO function for RV64 doubleword and word operands.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_atomic_tb;
  localparam logic [1:0] WORD = 2'd2;
  localparam logic [1:0] DOUBLE = 2'd3;
  localparam logic [3:0] SWAP = 4'd0;
  localparam logic [3:0] ADD = 4'd1;
  localparam logic [3:0] XOR = 4'd2;
  localparam logic [3:0] AND = 4'd3;
  localparam logic [3:0] OR = 4'd4;
  localparam logic [3:0] MIN = 4'd5;
  localparam logic [3:0] MAX = 4'd6;
  localparam logic [3:0] MINU = 4'd7;
  localparam logic [3:0] MAXU = 4'd8;

  logic [63:0] old_value;
  logic [63:0] operand;
  logic [1:0] memory_width;
  logic [3:0] operation;
  logic [63:0] value;

  RV5StageAtomicALU dut (.*);

  task automatic check_atomic(
    input logic [3:0] selected_operation,
    input logic [1:0] selected_width,
    input logic [63:0] selected_old,
    input logic [63:0] selected_operand,
    input logic [63:0] expected
  );
    begin
      operation = selected_operation;
      memory_width = selected_width;
      old_value = selected_old;
      operand = selected_operand;
      #1;
      assert (value == expected)
        else $fatal(1,
                    "atomic result mismatch op=%0d width=%0d old=%h operand=%h got=%h expected=%h",
                    operation, memory_width, old_value, operand, value, expected);
    end
  endtask

  initial begin
    check_atomic(SWAP, DOUBLE, 64'hf0, 64'h0f, 64'h0f);
    check_atomic(ADD, DOUBLE, 64'hf0, 64'h0f, 64'hff);
    check_atomic(XOR, DOUBLE, 64'hf0, 64'h0f, 64'hff);
    check_atomic(AND, DOUBLE, 64'hf0, 64'h0f, 64'h00);
    check_atomic(OR, DOUBLE, 64'hf0, 64'h0f, 64'hff);
    check_atomic(MIN, DOUBLE, 64'hfffffffffffffffe, 64'h3, 64'hfffffffffffffffe);
    check_atomic(MAX, DOUBLE, 64'hfffffffffffffffe, 64'h3, 64'h3);
    check_atomic(MINU, DOUBLE, 64'hfffffffffffffffe, 64'h3, 64'h3);
    check_atomic(MAXU, DOUBLE, 64'hfffffffffffffffe, 64'h3, 64'hfffffffffffffffe);
    check_atomic(ADD, WORD, 64'h00000000ffffffff, 64'h2, 64'h1);
    check_atomic(MIN, WORD, 64'h00000000fffffffe, 64'h3, 64'h00000000fffffffe);
    check_atomic(MAXU, WORD, 64'h00000000fffffffe, 64'h3, 64'h00000000fffffffe);
    $display("RV5Stage atomic ALU simulation passed");
    $finish;
  end
endmodule
