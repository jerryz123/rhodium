// Simulates enable, hold, shifting, and synchronous reset in the generated shift register.
// SPDX-License-Identifier: Apache-2.0
module enable_shift_register_tb;
    logic [3:0] data_in;
    logic shift;
    logic clk = 1'b0;
    logic reset = 1'b1;
    logic [3:0] data_out;

    EnableShiftRegister dut (
        .data_in(data_in),
        .shift(shift),
        .clk(clk),
        .reset(reset),
        .data_out(data_out)
    );

    always #5 clk = ~clk;

    initial begin
        data_in = 4'd0;
        shift = 1'b0;
        repeat (2) @(posedge clk);
        #1;
        assert (data_out == 4'd0) else $fatal(1, "synchronous reset failed");

        reset = 1'b0;
        shift = 1'b1;
        data_in = 4'd1;
        @(posedge clk);
        #1;
        data_in = 4'd2;
        @(posedge clk);
        #1;
        data_in = 4'd3;
        @(posedge clk);
        #1;
        data_in = 4'd4;
        @(posedge clk);
        #1;
        assert (data_out == 4'd1) else $fatal(1, "four-stage shift failed");

        shift = 1'b0;
        data_in = 4'd9;
        repeat (2) @(posedge clk);
        #1;
        assert (data_out == 4'd1) else $fatal(1, "disabled register did not hold");

        shift = 1'b1;
        data_in = 4'd5;
        @(posedge clk);
        #1;
        assert (data_out == 4'd2) else $fatal(1, "shift did not resume");

        reset = 1'b1;
        @(posedge clk);
        #1;
        assert (data_out == 4'd0) else $fatal(1, "second synchronous reset failed");

        $display("enable shift register simulation passed");
        $finish;
    end
endmodule
