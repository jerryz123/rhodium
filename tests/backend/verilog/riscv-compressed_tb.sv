// Checks representative Zca and profile-dependent full-C expansions.
// SPDX-License-Identifier: Apache-2.0
module riscv_compressed_tb;
  logic [15:0] compressed;
  RiscvCompressedExpansion rv32f;
  RiscvCompressedExpansion rv32d;
  RiscvCompressedExpansion rv64d;
  RiscvCompressedExpansion rv64_zca;
  RiscvCompressedExpansion rv64_zcb;

  RiscvCompressedFixture dut (.*);

  task automatic check_both(input logic [15:0] encoding,
                            input logic [31:0] expected);
    compressed = encoding;
    #1;
    assert (rv32f.valid && rv32f.instruction == expected)
      else $fatal(1, "RV32 expansion of %h was valid=%b instruction=%h", encoding, rv32f.valid, rv32f.instruction);
    assert (rv32d.valid && rv32d.instruction == expected)
      else $fatal(1, "RV32D expansion of %h was valid=%b instruction=%h", encoding, rv32d.valid, rv32d.instruction);
    assert (rv64d.valid && rv64d.instruction == expected)
      else $fatal(1, "RV64 expansion of %h was valid=%b instruction=%h", encoding, rv64d.valid, rv64d.instruction);
    assert (rv64_zca.valid && rv64_zca.instruction == expected)
      else $fatal(1, "RV64 Zca expansion of %h was valid=%b instruction=%h", encoding, rv64_zca.valid, rv64_zca.instruction);
    assert (rv64_zcb.valid && rv64_zcb.instruction == expected)
      else $fatal(1, "RV64 Zcb expansion of %h was valid=%b instruction=%h", encoding, rv64_zcb.valid, rv64_zcb.instruction);
  endtask

  initial begin
    check_both(16'h0085, 32'h00108093);
    check_both(16'h8082, 32'h00008067);
    check_both(16'h9082, 32'h000080e7);
    check_both(16'h829a, 32'h006002b3);
    check_both(16'h929a, 32'h006282b3);
    check_both(16'h9002, 32'h00100073);
    check_both(16'h6005, 32'h00001037);

    compressed = 16'h0000;
    #1;
    assert (!rv32f.valid && !rv32d.valid && !rv64d.valid && !rv64_zca.valid && !rv64_zcb.valid)
      else $fatal(1, "reserved c.addi4spn was accepted");

    compressed = 16'h2085;
    #1;
    assert (rv32f.valid && rv32f.instruction == 32'h060000ef)
      else $fatal(1, "RV32 c.jal expansion mismatch: %h", rv32f.instruction);
    assert (rv64d.valid && rv64d.instruction == 32'h0010809b)
      else $fatal(1, "RV64 c.addiw expansion mismatch: %h", rv64d.instruction);
    assert (rv64_zca.valid && rv64_zca.instruction == 32'h0010809b)
      else $fatal(1, "RV64 Zca c.addiw expansion mismatch: %h", rv64_zca.instruction);

    compressed = 16'h6000;
    #1;
    assert (rv32f.valid && rv32f.instruction == 32'h00042407)
      else $fatal(1, "RV32 c.flw expansion mismatch: %h", rv32f.instruction);
    assert (rv64d.valid && rv64d.instruction == 32'h00043403)
      else $fatal(1, "RV64 c.ld expansion mismatch: %h", rv64d.instruction);
    assert (rv64_zca.valid && rv64_zca.instruction == 32'h00043403)
      else $fatal(1, "RV64 Zca c.ld expansion mismatch: %h", rv64_zca.instruction);

    compressed = 16'h2000;
    #1;
    assert (!rv32f.valid)
      else $fatal(1, "RV32F unexpectedly accepted c.fld");
    assert (rv32d.valid && rv32d.instruction == 32'h00043407)
      else $fatal(1, "RV32D c.fld expansion mismatch: %h", rv32d.instruction);
    assert (rv64d.valid && rv64d.instruction == 32'h00043407)
      else $fatal(1, "RV64D c.fld expansion mismatch: %h", rv64d.instruction);
    assert (!rv64_zca.valid)
      else $fatal(1, "RV64 Zca unexpectedly accepted c.fld");
    assert (!rv64_zcb.valid)
      else $fatal(1, "RV64 Zcb unexpectedly accepted c.fld");

    compressed = 16'h8060;
    #1;
    assert (!rv64_zca.valid && rv64_zcb.valid && rv64_zcb.instruction == 32'h00344403)
      else $fatal(1, "RV64 c.lbu expansion mismatch");

    compressed = 16'h9c41;
    #1;
    assert (!rv64_zca.valid && rv64_zcb.valid && rv64_zcb.instruction == 32'h02840433)
      else $fatal(1, "RV64 c.mul expansion mismatch");

    compressed = 16'h9c71;
    #1;
    assert (!rv64_zca.valid && rv64_zcb.valid && rv64_zcb.instruction == 32'h0804043b)
      else $fatal(1, "RV64 c.zext.w expansion mismatch");

    $finish;
  end
endmodule
