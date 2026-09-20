// Simulates an enabled dynamic vector-register write and conditional hold.
// SPDX-License-Identifier: Apache-2.0
module vector_register_update_tb;
    logic clock = 1'b0;
    logic reset = 1'b1;
    logic write_enable = 1'b0;
    logic [1:0] selector = 2'd0;
    logic [3:0] replacement = 4'h0;
    logic [2:0][3:0] result;

    IndexedVectorRegister dut (
        .clock(clock),
        .reset(reset),
        .write_enable(write_enable),
        .selector(selector),
        .replacement(replacement),
        .result(result)
    );

    always #5 clock = ~clock;

    initial begin
        @(posedge clock);
        #1;
        assert (result == '0)
            else $fatal(1, "dynamic vector register reset failed");

        reset = 1'b0;
        write_enable = 1'b1;
        selector = 2'd1;
        replacement = 4'h9;
        @(posedge clock);
        #1;
        assert (result[0] == 4'h0 && result[1] == 4'h9 && result[2] == 4'h0)
            else $fatal(1, "dynamic vector register write failed");

        write_enable = 1'b0;
        selector = 2'd2;
        replacement = 4'h5;
        @(posedge clock);
        #1;
        assert (result[0] == 4'h0 && result[1] == 4'h9 && result[2] == 4'h0)
            else $fatal(1, "disabled dynamic vector register write did not hold");

        write_enable = 1'b1;
        @(posedge clock);
        #1;
        assert (result[0] == 4'h0 && result[1] == 4'h9 && result[2] == 4'h5)
            else $fatal(1, "last dynamic vector register write failed");

        $display("dynamic vector register simulation passed");
        $finish;
    end
endmodule
