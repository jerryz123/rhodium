// Simulates the host-generated eight-bit ripple-carry adder after CIRCT export.
// SPDX-License-Identifier: Apache-2.0
module generated_adder_tb;
    logic [7:0] A;
    logic [7:0] B;
    logic Cin;
    logic [7:0] Sum;
    logic Cout;

    Adder dut (
        .A(A),
        .B(B),
        .Cin(Cin),
        .Sum(Sum),
        .Cout(Cout)
    );

    task check_add(
        input logic [7:0] left,
        input logic [7:0] right,
        input logic carry_in,
        input logic [8:0] expected
    );
        begin
            A = left;
            B = right;
            Cin = carry_in;
            #1;
            assert ({Cout, Sum} == expected)
                else $fatal(1, "generated adder mismatch: %0d + %0d + %0d",
                            left, right, carry_in);
        end
    endtask

    initial begin
        check_add(8'd3, 8'd5, 1'b0, 9'd8);
        check_add(8'd255, 8'd1, 1'b0, 9'd256);
        check_add(8'd128, 8'd127, 1'b1, 9'd256);
        check_add(8'd85, 8'd170, 1'b0, 9'd255);

        $display("generated adder simulation passed");
        $finish;
    end
endmodule
