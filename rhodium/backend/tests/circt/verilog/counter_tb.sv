// Simulates enable, variable increments, modular wrap, and synchronous reset.
// SPDX-License-Identifier: Apache-2.0
module counter_tb;
    logic clock = 1'b0;
    logic reset = 1'b1;
    logic inc;
    logic [3:0] amt;
    logic [7:0] tot;

    Counter dut (
        .clock(clock),
        .reset(reset),
        .inc(inc),
        .amt(amt),
        .tot(tot)
    );

    always #5 clock = ~clock;

    initial begin
        inc = 1'b0;
        amt = 4'd3;
        repeat (2) @(posedge clock);
        #1;
        assert (tot == 8'd0) else $fatal(1, "synchronous reset failed");

        reset = 1'b0;
        repeat (2) @(posedge clock);
        #1;
        assert (tot == 8'd0) else $fatal(1, "disabled counter did not hold");

        inc = 1'b1;
        repeat (3) @(posedge clock);
        #1;
        assert (tot == 8'd9) else $fatal(1, "variable increment failed");

        amt = 4'd15;
        repeat (16) @(posedge clock);
        #1;
        assert (tot == 8'd249) else $fatal(1, "counter accumulation failed");

        amt = 4'd7;
        @(posedge clock);
        #1;
        assert (tot == 8'd0) else $fatal(1, "modular wrap failed");

        reset = 1'b1;
        @(posedge clock);
        #1;
        assert (tot == 8'd0) else $fatal(1, "second synchronous reset failed");

        $display("counter simulation passed");
        $finish;
    end
endmodule
