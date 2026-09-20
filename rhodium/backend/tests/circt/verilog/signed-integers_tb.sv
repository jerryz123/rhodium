// Simulates signed overflow, ordering, arithmetic shifts, and explicit resizing.
// SPDX-License-Identifier: Apache-2.0
module signed_integers_tb;
    logic signed [7:0] a;
    logic signed [7:0] b;
    logic [2:0] amount;
    logic [11:0] wide_amount;
    logic signed [7:0] sum;
    logic signed [7:0] difference;
    logic signed [7:0] product;
    logic signed [7:0] left;
    logic signed [7:0] right;
    logic signed [7:0] right_wide;
    logic signed [11:0] widened;
    logic signed [3:0] narrowed;
    logic lt;
    logic gt;
    logic le;
    logic ge;
    logic eq;
    logic signed [7:0] negative_one;

    SignedIntegers dut (
        .a(a), .b(b), .amount(amount), .wide_amount(wide_amount),
        .sum(sum), .difference(difference), .product(product),
        .left(left), .right(right), .right_wide(right_wide),
        .widened(widened), .narrowed(narrowed),
        .lt(lt), .gt(gt), .le(le), .ge(ge), .eq(eq),
        .negative_one(negative_one)
    );

    task automatic check_results(
        input logic signed [7:0] next_a,
        input logic signed [7:0] next_b,
        input logic [2:0] next_amount,
        input logic [11:0] next_wide_amount
    );
        logic signed [11:0] expected_widened;
        a = next_a;
        b = next_b;
        amount = next_amount;
        wide_amount = next_wide_amount;
        expected_widened = {{4{next_a[7]}}, next_a};
        #1;
        assert (sum == $signed(next_a + next_b)) else $fatal(1, "signed add failed");
        assert (difference == $signed(next_a - next_b)) else $fatal(1, "signed subtract failed");
        assert (product == $signed(next_a * next_b)) else $fatal(1, "signed multiply failed");
        assert (left == $signed(next_a << next_amount)) else $fatal(1, "signed left shift failed");
        assert (right == $signed(next_a >>> next_amount)) else $fatal(1, "signed right shift failed");
        assert (right_wide == $signed(next_a >>> next_wide_amount)) else $fatal(1, "wide signed right shift failed");
        assert (widened == expected_widened) else $fatal(1, "sign extension failed");
        assert (narrowed == next_a[3:0]) else $fatal(1, "signed truncation failed");
        assert (lt == (next_a < next_b)) else $fatal(1, "signed less-than failed");
        assert (gt == (next_a > next_b)) else $fatal(1, "signed greater-than failed");
        assert (le == (next_a <= next_b)) else $fatal(1, "signed less-or-equal failed");
        assert (ge == (next_a >= next_b)) else $fatal(1, "signed greater-or-equal failed");
        assert (eq == (next_a == next_b)) else $fatal(1, "signed equality failed");
        assert (negative_one == -1) else $fatal(1, "signed literal failed");
    endtask

    initial begin
        check_results(-8'sd5, 8'sd3, 3'd1, 12'd1);
        check_results(-8'sd128, 8'sd127, 3'd7, 12'd12);
        check_results(8'sd100, 8'sd40, 3'd2, 12'd20);
        $display("signed integer simulation passed");
        $finish;
    end
endmodule
