// Simulates ordinary and overflowing unsigned fixed-width multiplication.
// SPDX-License-Identifier: Apache-2.0
module multiply_tb;
    logic [7:0] a;
    logic [7:0] b;
    logic [7:0] product;

    Multiplier dut (
        .a(a),
        .b(b),
        .product(product)
    );

    initial begin
        a = 8'd7;
        b = 8'd9;
        #1;
        assert (product == 8'd63) else $fatal(1, "ordinary multiplication failed");

        a = 8'd200;
        b = 8'd3;
        #1;
        assert (product == 8'd88) else $fatal(1, "modular overflow failed");

        a = 8'd255;
        b = 8'd255;
        #1;
        assert (product == 8'd1) else $fatal(1, "maximum operand overflow failed");

        $display("fixed-width multiplication simulation passed");
        $finish;
    end
endmodule
