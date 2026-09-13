// Simulates only the output bits constrained by the typed decode relation.
// SPDX-License-Identifier: Apache-2.0
module decode_tb;
    logic [4:0] encoded;
    logic [1:0] alu;
    logic write;

    InstructionDecoder dut (
        .encoded(encoded),
        .alu(alu),
        .write(write)
    );

    initial begin
        encoded = 5'b00000;
        #1;
        assert (alu == 2'b01 && write == 1'b0)
            else $fatal(1, "add decode failed");

        encoded = 5'b00001;
        #1;
        assert (alu == 2'b01 && write == 1'b0)
            else $fatal(1, "input wildcard failed");

        encoded = 5'b00010;
        #1;
        assert (alu == 2'b10)
            else $fatal(1, "sub decode failed");

        encoded = 5'b11110;
        #1;
        assert (alu == 2'b00)
            else $fatal(1, "default decode failed");

        $display("Decode simulation passed");
        $finish;
    end
endmodule
