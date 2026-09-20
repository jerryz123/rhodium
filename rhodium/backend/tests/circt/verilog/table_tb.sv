// Simulates every address of the host-generated identity byte table.
// SPDX-License-Identifier: Apache-2.0
module table_tb;
    logic [7:0] addr;
    logic [7:0] out;

    Tbl dut (
        .addr(addr),
        .out(out)
    );

    initial begin
        for (int i = 0; i < 256; i++) begin
            addr = i[7:0];
            #1;
            assert (out == i[7:0])
                else $fatal(1, "table lookup failed at address %0d", i);
        end

        $display("table simulation passed");
        $finish;
    end
endmodule
