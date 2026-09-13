// Simulates vector construction, selection, packing, projection, and registers after CIRCT export.
// SPDX-License-Identifier: Apache-2.0
module vector_tb;
    logic clk = 1'b0;
    logic reset = 1'b1;
    logic [1:0] selector;
    logic [3:0] a;
    logic [3:0] b;
    logic [3:0] c;
    logic [2:0][3:0] alternate;
    logic [2:0][3:0] assembled;
    logic [2:0][3:0] selected;
    logic [3:0] chosen;
    logic [11:0] packed_value;
    logic [2:0][3:0] restored;
    logic [2:0][3:0] reversed;
    logic [2:0][3:0] state_out;

    VectorDatapath dut (
        .clk(clk),
        .reset(reset),
        .selector(selector),
        .a(a),
        .b(b),
        .c(c),
        .alternate(alternate),
        .assembled(assembled),
        .selected(selected),
        .chosen(chosen),
        .packed_0(packed_value),
        .restored(restored),
        .reversed(reversed),
        .state_out(state_out)
    );

    always #5 clk = ~clk;

    initial begin
        a = 4'h1;
        b = 4'h2;
        c = 4'h3;
        alternate[0] = 4'h4;
        alternate[1] = 4'h5;
        alternate[2] = 4'h6;
        selector = 2'd0;

        #1;
        assert (assembled[0] == a && assembled[1] == b && assembled[2] == c)
            else $fatal(1, "vector construction or static indexing failed");
        assert (packed_value == 12'h321)
            else $fatal(1, "element zero is not the least-significant packed element");
        assert (restored == assembled)
            else $fatal(1, "vector packing round trip failed");
        assert (reversed[0] == c && reversed[1] == b && reversed[2] == a)
            else $fatal(1, "element-wise vector output failed");
        assert (chosen == a)
            else $fatal(1, "vector lookup at index zero failed");

        @(posedge clk);
        #1;
        assert (state_out == assembled)
            else $fatal(1, "vector register synchronous reset failed");

        reset = 1'b0;
        selector = 2'd1;
        #1;
        assert (selected == alternate && chosen == b)
            else $fatal(1, "whole-vector mux or dynamic lookup failed");
        @(posedge clk);
        #1;
        assert (state_out == alternate)
            else $fatal(1, "vector register update failed");

        selector = 2'd2;
        #1;
        assert (selected == assembled && chosen == c)
            else $fatal(1, "last dynamic vector element failed");

        $display("vector simulation passed");
        $finish;
    end
endmodule
