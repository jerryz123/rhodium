// Simulates every operation in the eight-bit Rhodium ALU after CIRCT export.
// SPDX-License-Identifier: Apache-2.0
// Simulates the CIRCT-exported canonical ALU module.
module alu_tb;
    logic [7:0] a;
    logic [7:0] b;
    logic [2:0] op;
    logic [7:0] result;
    logic equal;

    ALU dut (
        .a(a),
        .b(b),
        .op(op),
        .result(result),
        .equal(equal)
    );

    task automatic check_operation(
        input logic [2:0] selected_op,
        input logic [7:0] expected,
        input string label
    );
        op = selected_op;
        #1;
        assert (result == expected) else $fatal(1, "%s failed", label);
    endtask

    initial begin
        a = 8'hcc;
        b = 8'haa;
        #1;
        assert (!equal) else $fatal(1, "unequal comparison failed");

        check_operation(3'd0, 8'h88, "AND");
        check_operation(3'd1, 8'hee, "OR");
        check_operation(3'd2, 8'h66, "XOR");
        check_operation(3'd3, 8'h76, "modular ADD");
        check_operation(3'd4, 8'h22, "modular SUB");
        check_operation(3'd5, 8'h33, "NOT-A");

        a = 8'h5a;
        b = 8'h5a;
        op = 3'd3;
        #1;
        assert (equal) else $fatal(1, "equal comparison failed");
        assert (result == 8'hb4) else $fatal(1, "equal-input ADD failed");

        $display("ALU simulation passed");
        $finish;
    end
endmodule
