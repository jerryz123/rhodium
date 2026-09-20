// Simulates carry preservation and full-width unsigned and signed multiplication.
// SPDX-License-Identifier: Apache-2.0
module expanding_arithmetic_tb;
    logic [7:0] a;
    logic [3:0] b;
    logic signed [7:0] signed_a;
    logic signed [3:0] signed_b;
    logic [8:0] sum;
    logic [11:0] product;
    logic signed [11:0] signed_product;

    ExpandingArithmetic dut (
        .a(a),
        .b(b),
        .signed_a(signed_a),
        .signed_b(signed_b),
        .sum(sum),
        .product(product),
        .signed_product(signed_product)
    );

    task automatic check_results(
        input logic [7:0] next_a,
        input logic [3:0] next_b,
        input logic signed [7:0] next_signed_a,
        input logic signed [3:0] next_signed_b,
        input logic [8:0] expected_sum,
        input logic [11:0] expected_product,
        input logic signed [11:0] expected_signed_product
    );
        a = next_a;
        b = next_b;
        signed_a = next_signed_a;
        signed_b = next_signed_b;
        #1;
        assert (sum == expected_sum) else $fatal(1, "expanding addition failed");
        assert (product == expected_product) else $fatal(1, "expanding multiplication failed");
        assert (signed_product == expected_signed_product) else $fatal(1, "signed expanding multiplication failed");
    endtask

    initial begin
        check_results(8'd0, 4'd0, 8'sd0, 4'sd0, 9'd0, 12'd0, 12'sd0);
        check_results(8'd255, 4'd15, -8'sd128, 4'sd7, 9'd270, 12'd3825, -12'sd896);
        check_results(8'd128, 4'd8, 8'sd127, -4'sd8, 9'd136, 12'd1024, -12'sd1016);

        $display("expanding arithmetic simulation passed");
        $finish;
    end
endmodule
