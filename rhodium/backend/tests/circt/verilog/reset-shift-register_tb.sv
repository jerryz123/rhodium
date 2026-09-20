// Simulates reset, shifting, and implicit register hold in ResetShiftRegister.
// SPDX-License-Identifier: Apache-2.0
module reset_shift_register_tb;
    logic clock = 1'b0;
    logic reset = 1'b1;
    logic [3:0] data_in;
    logic shift;
    logic [3:0] data_out;

    ResetShiftRegister dut (
        .clock(clock),
        .reset(reset),
        .data_in(data_in),
        .shift(shift),
        .data_out(data_out)
    );

    always #5 clock = ~clock;

    initial begin
        data_in = 4'd0;
        shift = 1'b0;
        repeat (2) @(posedge clock);
        #1;
        assert (data_out == 4'd0) else $fatal(1, "synchronous reset failed");

        reset = 1'b0;
        shift = 1'b1;
        data_in = 4'd1;
        @(posedge clock);
        #1;
        data_in = 4'd2;
        @(posedge clock);
        #1;
        data_in = 4'd3;
        @(posedge clock);
        #1;
        data_in = 4'd4;
        @(posedge clock);
        #1;
        assert (data_out == 4'd1) else $fatal(1, "four-stage shift failed");

        shift = 1'b0;
        data_in = 4'd9;
        repeat (2) @(posedge clock);
        #1;
        assert (data_out == 4'd1) else $fatal(1, "disabled register did not hold");

        reset = 1'b1;
        @(posedge clock);
        #1;
        assert (data_out == 4'd0) else $fatal(1, "second synchronous reset failed");

        $display("reset shift register simulation passed");
        $finish;
    end
endmodule
