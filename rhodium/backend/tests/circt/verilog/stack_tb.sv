// Simulates stack push, pop, output timing, enable gating, and full protection.
// SPDX-License-Identifier: Apache-2.0
module stack_tb;
    logic clock = 1'b0;
    logic reset = 1'b1;
    logic push;
    logic pop;
    logic en;
    logic [31:0] data_in;
    logic [31:0] data_out;

    Stack dut (
        .clock(clock),
        .reset(reset),
        .push(push),
        .pop(pop),
        .en(en),
        .data_in(data_in),
        .data_out(data_out)
    );

    always #5 clock = ~clock;

    task cycle;
        input logic next_push;
        input logic next_pop;
        input logic next_en;
        input logic [31:0] next_data;
        begin
            push = next_push;
            pop = next_pop;
            en = next_en;
            data_in = next_data;
            @(posedge clock);
            #1;
        end
    endtask

    initial begin
        push = 1'b0;
        pop = 1'b0;
        en = 1'b0;
        data_in = 32'b0;
        repeat (2) @(posedge clock);
        #1;
        assert (data_out == 32'b0) else $fatal(1, "synchronous reset failed");

        reset = 1'b0;
        cycle(1'b1, 1'b0, 1'b1, 32'h11);
        assert (data_out == 32'h0) else $fatal(1, "first push changed the registered output early");
        cycle(1'b0, 1'b0, 1'b1, 32'h0);
        assert (data_out == 32'h11) else $fatal(1, "first top value was not observed");

        cycle(1'b1, 1'b0, 1'b1, 32'h22);
        assert (data_out == 32'h11) else $fatal(1, "push did not retain the previous top for its cycle");
        cycle(1'b0, 1'b0, 1'b1, 32'h0);
        assert (data_out == 32'h22) else $fatal(1, "second top value was not observed");

        cycle(1'b0, 1'b1, 1'b1, 32'h0);
        assert (data_out == 32'h22) else $fatal(1, "pop did not expose the popped word");
        cycle(1'b0, 1'b0, 1'b1, 32'h0);
        assert (data_out == 32'h11) else $fatal(1, "pop did not reveal the preceding word");

        cycle(1'b1, 1'b0, 1'b0, 32'hDEAD_BEEF);
        cycle(1'b0, 1'b0, 1'b1, 32'h0);
        assert (data_out == 32'h11) else $fatal(1, "disabled push changed the stack");

        cycle(1'b1, 1'b0, 1'b1, 32'h22);
        cycle(1'b1, 1'b0, 1'b1, 32'h33);
        cycle(1'b1, 1'b0, 1'b1, 32'h44);
        cycle(1'b1, 1'b0, 1'b1, 32'h55);
        cycle(1'b0, 1'b0, 1'b1, 32'h0);
        assert (data_out == 32'h44) else $fatal(1, "push beyond full depth changed the top");

        $display("stack simulation passed");
        $finish;
    end
endmodule
