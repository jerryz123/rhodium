// Simulates a vector assembled through an internal Rhodium wire after CIRCT export.
// SPDX-License-Identifier: Apache-2.0
module wire_tb;
    logic [3:0] left;
    logic [3:0] right;
    logic [3:0] sum;

    VectorWire dut (
        .left(left),
        .right(right),
        .sum(sum)
    );

    initial begin
        left = 4'b1010;
        right = 4'b1100;
        #1;
        assert (sum == 4'b0110)
            else $fatal(1, "element-wise vector wire assembly failed");

        left = 4'b1111;
        right = 4'b0101;
        #1;
        assert (sum == 4'b1010)
            else $fatal(1, "vector wire update failed");

        $display("wire simulation passed");
        $finish;
    end
endmodule
