// Simulates the FIR example's positive and negative impulse responses.
// SPDX-License-Identifier: Apache-2.0
module fir_filter_tb;
    logic clock = 1'b0;
    logic reset = 1'b1;
    logic signed [7:0] sample;
    logic signed [12:0] filtered;

    FirFilter dut (
        .clock(clock),
        .reset(reset),
        .sample(sample),
        .filtered(filtered)
    );

    always #5 clock = ~clock;

    task automatic check_sample(
        input logic signed [7:0] next_sample,
        input logic signed [12:0] expected
    );
        sample = next_sample;
        #1;
        assert (filtered == expected) else
            $fatal(1, "FIR output mismatch: got %0d, expected %0d", filtered, expected);
        @(posedge clock);
        #1;
    endtask

    task automatic apply_reset;
        reset = 1'b1;
        sample = 8'sd0;
        @(posedge clock);
        #1;
        assert (filtered == 13'sd0) else $fatal(1, "FIR reset failed");
        reset = 1'b0;
    endtask

    initial begin
        apply_reset();
        check_sample(8'sd8, 13'sd8);
        check_sample(8'sd0, 13'sd24);
        check_sample(8'sd0, 13'sd24);
        check_sample(8'sd0, 13'sd8);
        check_sample(8'sd0, 13'sd0);

        apply_reset();
        check_sample(-8'sd8, -13'sd8);
        check_sample(8'sd0, -13'sd24);
        check_sample(8'sd0, -13'sd24);
        check_sample(8'sd0, -13'sd8);
        check_sample(8'sd0, 13'sd0);

        $display("FIR filter simulation passed");
        $finish;
    end
endmodule
