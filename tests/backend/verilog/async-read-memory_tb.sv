// Simulates asynchronous reads, enabled writes, and disabled writes for memory.
// SPDX-License-Identifier: Apache-2.0
module async_read_memory_tb;
    logic clock = 1'b0;
    logic [1:0] read_address;
    logic [1:0] write_address;
    logic [7:0] write_data;
    logic write_enable;
    logic [7:0] read_data;

    AsyncReadMemory dut (
        .clock(clock),
        .read_address(read_address),
        .write_address(write_address),
        .write_data(write_data),
        .write_enable(write_enable),
        .read_data(read_data)
    );

    always #5 clock = ~clock;

    initial begin
        read_address = 2'd1;
        write_address = 2'd1;
        write_data = 8'hA5;
        write_enable = 1'b1;
        @(posedge clock);
        #1;
        assert (read_data == 8'hA5) else $fatal(1, "enabled write was not visible");

        write_enable = 1'b0;
        write_data = 8'h3C;
        @(posedge clock);
        #1;
        assert (read_data == 8'hA5) else $fatal(1, "disabled write changed memory");

        write_address = 2'd2;
        write_data = 8'h5A;
        write_enable = 1'b1;
        @(posedge clock);
        #1;
        assert (read_data == 8'hA5) else $fatal(1, "write changed the unselected read word");

        read_address = 2'd2;
        #1;
        assert (read_data == 8'h5A) else $fatal(1, "async address change did not update read data");

        $display("async-read memory simulation passed");
        $finish;
    end
endmodule
