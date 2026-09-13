// Simulates equality boundaries and unsigned high-bit ordering comparisons.
// SPDX-License-Identifier: Apache-2.0
module unsigned_comparisons_tb;
    logic [7:0] a;
    logic [7:0] b;
    logic lt;
    logic gt;
    logic le;
    logic ge;

    UnsignedComparisons dut (
        .a(a),
        .b(b),
        .lt(lt),
        .gt(gt),
        .le(le),
        .ge(ge)
    );

    task automatic check_results(
        input logic [7:0] next_a,
        input logic [7:0] next_b,
        input logic expected_lt,
        input logic expected_gt,
        input logic expected_le,
        input logic expected_ge
    );
        a = next_a;
        b = next_b;
        #1;
        assert (lt == expected_lt) else $fatal(1, "unexpected unsigned less-than result");
        assert (gt == expected_gt) else $fatal(1, "unexpected unsigned greater-than result");
        assert (le == expected_le) else $fatal(1, "unexpected unsigned less-or-equal result");
        assert (ge == expected_ge) else $fatal(1, "unexpected unsigned greater-or-equal result");
    endtask

    initial begin
        check_results(8'd0, 8'd0, 1'b0, 1'b0, 1'b1, 1'b1);
        check_results(8'd0, 8'd255, 1'b1, 1'b0, 1'b1, 1'b0);
        check_results(8'd255, 8'd0, 1'b0, 1'b1, 1'b0, 1'b1);
        check_results(8'd127, 8'd128, 1'b1, 1'b0, 1'b1, 1'b0);

        $display("unsigned comparison simulation passed");
        $finish;
    end
endmodule
