// Simulates carry propagation through the four FullAdder instances.
// SPDX-License-Identifier: Apache-2.0
module adder4_tb;
  logic [3:0] A;
  logic [3:0] B;
  logic Cin;
  logic [3:0] Sum;
  logic Cout;

  Adder4 dut (
    .A(A),
    .B(B),
    .Cin(Cin),
    .Sum(Sum),
    .Cout(Cout)
  );

  task check_add(
    input logic [3:0] left,
    input logic [3:0] right,
    input logic carry_in,
    input logic [4:0] expected
  );
    begin
      A = left;
      B = right;
      Cin = carry_in;
      #1;
      if ({Cout, Sum} !== expected)
        $fatal(1, "adder mismatch: %0d + %0d + %0d", left, right, carry_in);
    end
  endtask

  initial begin
    check_add(4'd3, 4'd5, 1'b0, 5'd8);
    check_add(4'd15, 4'd1, 1'b0, 5'd16);
    check_add(4'd9, 4'd6, 1'b1, 5'd16);
    check_add(4'd7, 4'd4, 1'b1, 5'd12);

    $display("Adder4 simulation passed");
    $finish;
  end
endmodule
