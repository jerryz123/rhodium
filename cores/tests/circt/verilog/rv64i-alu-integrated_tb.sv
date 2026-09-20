// Simulates decoded RV64I controls through the ALU, including unconstrained modifiers.
// SPDX-License-Identifier: Apache-2.0
module rv64i_alu_integrated_tb;
    logic [31:0] instruction;
    logic [63:0] left;
    logic [63:0] right;
    logic valid;
    logic [63:0] result;

    DecodedALU dut (
        .instruction(instruction),
        .left(left),
        .right(right),
        .valid(valid),
        .result(result)
    );

    task automatic check_alu(
        input logic [31:0] instruction_in,
        input logic [63:0] left_in,
        input logic [63:0] right_in,
        input logic [63:0] expected
    );
        instruction = instruction_in;
        left = left_in;
        right = right_in;
        #1;
        assert (valid === 1'b1)
            else $fatal(1, "instruction %h did not decode", instruction_in);
        assert (result === expected)
            else $fatal(1,
                        "instruction %h failed: left=%h right=%h result=%h expected=%h",
                        instruction_in, left_in, right_in, result, expected);
    endtask

    initial begin
        check_alu(32'h00000013, 64'd41, 64'd1, 64'd42); // ADDI
        check_alu(32'h40000033, 64'd0, 64'd1, 64'hFFFFFFFFFFFFFFFF); // SUB
        check_alu(32'h00001013, 64'd1, 64'd7, 64'd128); // SLLI
        check_alu(32'h00002033, 64'hFFFFFFFFFFFFFFFF, 64'd0, 64'd1); // SLT
        check_alu(32'h00003033, 64'd0, 64'hFFFFFFFFFFFFFFFF, 64'd1); // SLTU
        check_alu(32'h00004013, 64'hAA55, 64'h0F0F, 64'hA55A); // XORI
        check_alu(32'h40005013, 64'h8000000000000000, 64'd1, 64'hC000000000000000); // SRAI
        check_alu(32'h00006013, 64'hF000, 64'h0F0F, 64'hFF0F); // ORI
        check_alu(32'h00007013, 64'hF0F0, 64'h0FF0, 64'h00F0); // ANDI
        check_alu(32'h0000001B, 64'h000000007FFFFFFF, 64'd1, 64'hFFFFFFFF80000000); // ADDIW
        check_alu(32'h4000501B, 64'h0000000080000000, 64'd1, 64'hFFFFFFFFC0000000); // SRAIW

        instruction = 32'hFFFFFFFF;
        left = '0;
        right = '0;
        #1;
        assert (valid === 1'b0)
            else $fatal(1, "unsupported instruction unexpectedly decoded");

        $display("integrated RV64I decoder and ALU simulation passed");
        $finish;
    end
endmodule
