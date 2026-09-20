// Simulates selected replacement and out-of-range preservation for vector updates.
// SPDX-License-Identifier: Apache-2.0
module vector_update_tb;
    logic [2:0][3:0] source;
    logic [1:0] selector;
    logic [3:0] replacement;
    logic [2:0][3:0] result;

    VectorUpdate dut (
        .source(source),
        .selector(selector),
        .replacement(replacement),
        .result(result)
    );

    initial begin
        source[0] = 4'h1;
        source[1] = 4'h2;
        source[2] = 4'h3;
        replacement = 4'h9;

        selector = 2'd1;
        #1;
        assert (result[0] == 4'h1 && result[1] == 4'h9 && result[2] == 4'h3)
            else $fatal(1, "selected vector replacement failed");

        selector = 2'd0;
        #1;
        assert (result[0] == 4'h9 && result[1] == 4'h2 && result[2] == 4'h3)
            else $fatal(1, "element-zero vector replacement failed");

        selector = 2'd3;
        #1;
        assert (result == source)
            else $fatal(1, "out-of-range vector update did not preserve source");

        $display("vector update simulation passed");
        $finish;
    end
endmodule
