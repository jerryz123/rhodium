// Simulates the CIRCT-exported explicitly reused module hierarchy.
// SPDX-License-Identifier: Apache-2.0
module hierarchy_tb;
    logic [7:0] a;
    logic [7:0] b;
    logic [7:0] c;
    logic [7:0] sum0;
    logic [7:0] sum1;

    TwoAdders dut (
        .a(a),
        .b(b),
        .c(c),
        .sum0(sum0),
        .sum1(sum1)
    );

    initial begin
        a = 8'd7;
        b = 8'd11;
        c = 8'd20;
        #1;
        assert (sum0 == 8'd18) else $fatal(1, "first instance failed");
        assert (sum1 == 8'd27) else $fatal(1, "second instance failed");

        a = 8'd250;
        b = 8'd10;
        c = 8'd20;
        #1;
        assert (sum0 == 8'd4) else $fatal(1, "first instance overflow failed");
        assert (sum1 == 8'd14) else $fatal(1, "second instance overflow failed");

        $display("hierarchy simulation passed");
        $finish;
    end
endmodule
