// Simulates vector load, shifting, load priority, and implicit register hold.
// SPDX-License-Identifier: Apache-2.0
module vec_shift_register_tb;
    logic clock = 1'b0;
    logic reset = 1'b0;
    logic [3:0][3:0] ins;
    logic load;
    logic shift;
    logic [3:0] out;

    VecShiftRegister dut (
        .clock(clock),
        .reset(reset),
        .ins(ins),
        .load(load),
        .shift(shift),
        .out(out)
    );

    always #5 clock = ~clock;

    task automatic tick;
        @(posedge clock);
        #1;
    endtask

    initial begin
        ins[0] = 4'h1;
        ins[1] = 4'h2;
        ins[2] = 4'h3;
        ins[3] = 4'h4;
        load = 1'b1;
        shift = 1'b0;
        tick();
        assert (out == 4'h4) else $fatal(1, "vector load failed");

        load = 1'b0;
        shift = 1'b1;
        ins[0] = 4'h9;
        tick();
        assert (out == 4'h3) else $fatal(1, "first vector shift failed");
        tick();
        assert (out == 4'h2) else $fatal(1, "second vector shift failed");
        tick();
        assert (out == 4'h1) else $fatal(1, "third vector shift failed");
        tick();
        assert (out == 4'h9) else $fatal(1, "new input did not enter shift chain");

        ins[0] = 4'h5;
        ins[1] = 4'h6;
        ins[2] = 4'h7;
        ins[3] = 4'h8;
        load = 1'b1;
        shift = 1'b1;
        tick();
        assert (out == 4'h8) else $fatal(1, "load did not take priority");

        load = 1'b0;
        shift = 1'b0;
        ins[3] = 4'hf;
        tick();
        assert (out == 4'h8) else $fatal(1, "disabled vector register did not hold");

        $display("vector shift register simulation passed");
        $finish;
    end
endmodule
