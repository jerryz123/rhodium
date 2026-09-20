// Simulates the full eight-cycle sequence of the registered vector search.
// SPDX-License-Identifier: Apache-2.0
module vec_search_tb;
    logic clock = 1'b0;
    logic reset = 1'b1;
    logic [3:0] out;

    VecSearch dut (
        .clock(clock),
        .reset(reset),
        .out(out)
    );

    always #5 clock = ~clock;

    task check_next;
        input logic [3:0] expected;
        begin
            @(posedge clock);
            #1;
            assert (out == expected)
                else $fatal(1, "expected %0d, got %0d", expected, out);
        end
    endtask

    initial begin
        repeat (2) @(posedge clock);
        #1;
        assert (out == 4'd0) else $fatal(1, "synchronous reset failed");

        reset = 1'b0;
        check_next(4'd4);
        check_next(4'd15);
        check_next(4'd14);
        check_next(4'd2);
        check_next(4'd5);
        check_next(4'd13);
        check_next(4'd0);
        check_next(4'd0);

        $display("vector search simulation passed");
        $finish;
    end
endmodule
