// Simulates the CIRCT-exported canonical adder module.
// SPDX-License-Identifier: Apache-2.0
module adder_tb;
    logic [7:0] a;
    logic [7:0] b;
    logic [7:0] sum;

    Adder dut (
        .a(a),
        .b(b),
        .sum(sum)
    );

    initial begin
        a = 8'd1;
        b = 8'd2;
        #1;
        assert (sum == 8'd3) else $fatal(1, "1 + 2 failed");

        a = 8'd255;
        b = 8'd1;
        #1;
        assert (sum == 8'd0) else $fatal(1, "modular overflow failed");

        a = 8'd87;
        b = 8'd44;
        #1;
        assert (sum == 8'd131) else $fatal(1, "87 + 44 failed");

        $display("adder simulation passed");
        $finish;
    end
endmodule
