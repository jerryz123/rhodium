// Simulates simultaneous distinct-address writes through two physical memory ports.
// SPDX-License-Identifier: Apache-2.0
module multi_write_memory_tb;
    logic clock = 1'b0;
    logic [1:0] read_address;
    logic [1:0] first_address;
    logic [1:0] second_address;
    logic [7:0] first_data;
    logic [7:0] second_data;
    logic first_enable;
    logic second_enable;
    logic [7:0] read_data;

    MultiWriteMemory dut (
        .clock(clock),
        .read_address(read_address),
        .first_address(first_address),
        .second_address(second_address),
        .first_data(first_data),
        .second_data(second_data),
        .first_enable(first_enable),
        .second_enable(second_enable),
        .read_data(read_data)
    );

    always #5 clock = ~clock;

    initial begin
        read_address = 2'd0;
        first_address = 2'd0;
        second_address = 2'd1;
        first_data = 8'hA5;
        second_data = 8'h5A;
        first_enable = 1'b1;
        second_enable = 1'b1;

        @(posedge clock);
        #1;
        first_enable = 1'b0;
        second_enable = 1'b0;
        assert (read_data == 8'hA5)
            else $fatal(1, "first write port failed");

        read_address = 2'd1;
        #1;
        assert (read_data == 8'h5A)
            else $fatal(1, "second write port failed");

        first_address = 2'd2;
        first_data = 8'h3C;
        first_enable = 1'b1;
        @(posedge clock);
        #1;
        first_enable = 1'b0;
        read_address = 2'd2;
        #1;
        assert (read_data == 8'h3C)
            else $fatal(1, "independently enabled write port failed");

        $display("multi-write memory simulation passed");
        $finish;
    end
endmodule
