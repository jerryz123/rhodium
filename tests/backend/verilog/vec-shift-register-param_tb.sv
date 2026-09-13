// Simulates reset and cycle-by-cycle shifting in the parameterized vector register.
// SPDX-License-Identifier: Apache-2.0
module vec_shift_register_param_tb;
    logic clock = 1'b0;
    logic reset = 1'b1;
    logic [3:0] data_in;
    logic [3:0] data_out;

    VecShiftRegisterParam dut (
        .clock(clock),
        .reset(reset),
        .data_in(data_in),
        .data_out(data_out)
    );

    always #5 clock = ~clock;

    task automatic tick;
        @(posedge clock);
        #1;
    endtask

    initial begin
        data_in = 4'h0;
        tick();
        assert (data_out == 4'h0) else $fatal(1, "vector reset failed");

        reset = 1'b0;
        data_in = 4'h1;
        tick();
        assert (data_out == 4'h0) else $fatal(1, "first shift emerged too early");
        data_in = 4'h2;
        tick();
        assert (data_out == 4'h0) else $fatal(1, "second shift emerged too early");
        data_in = 4'h3;
        tick();
        assert (data_out == 4'h0) else $fatal(1, "third shift emerged too early");
        data_in = 4'h4;
        tick();
        assert (data_out == 4'h1) else $fatal(1, "four-stage shift failed");
        data_in = 4'h5;
        tick();
        assert (data_out == 4'h2) else $fatal(1, "shift pipeline did not advance");

        reset = 1'b1;
        tick();
        assert (data_out == 4'h0) else $fatal(1, "second vector reset failed");

        $display("parameterized vector shift register simulation passed");
        $finish;
    end
endmodule
