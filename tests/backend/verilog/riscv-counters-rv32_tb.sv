// Verifies RV32 counter halves, retirement increments, writes, and overflow.
// SPDX-License-Identifier: Apache-2.0
module riscv_counters_rv32_tb;
  typedef struct packed {
    logic [11:0] address;
    logic [31:0] value;
  } counter_write_bits_t;
  typedef struct packed {
    logic valid;
    counter_write_bits_t bits;
  } machine_write_in_t;

  localparam logic [11:0] CSR_MCYCLE = 12'hb00;
  localparam logic [11:0] CSR_MINSTRET = 12'hb02;
  localparam logic [11:0] CSR_MCYCLEH = 12'hb80;
  localparam logic [11:0] CSR_MINSTRETH = 12'hb82;

  logic clock = 1'b0;
  logic reset = 1'b1;
  logic retire;
  machine_write_in_t machine_write_in;
  logic [63:0] cycle;
  logic [63:0] instret;

  RiscvBaseCounters dut (.*);
  always #5 clock = ~clock;

  task automatic step(
    input logic retire_value,
    input logic write_valid,
    input logic [11:0] write_address,
    input logic [31:0] write_value
  );
    @(negedge clock);
    retire = retire_value;
    machine_write_in.valid = write_valid;
    machine_write_in.bits.address = write_address;
    machine_write_in.bits.value = write_value;
    @(posedge clock);
    #1;
  endtask

  initial begin
    retire = 1'b0;
    machine_write_in = '0;
    repeat (2) @(posedge clock);
    #1;
    reset = 1'b0;
    assert (cycle == 0 && instret == 0)
      else $fatal(1, "base counters did not reset to zero");

    step(1'b0, 1'b0, '0, '0);
    assert (cycle == 1 && instret == 0)
      else $fatal(1, "cycle or idle instret update was incorrect");
    step(1'b1, 1'b0, '0, '0);
    assert (cycle == 2 && instret == 1)
      else $fatal(1, "retirement did not increment instret");

    step(1'b1, 1'b1, CSR_MINSTRET, 32'hffffffff);
    assert (cycle == 3 && instret == 64'h00000000ffffffff)
      else $fatal(1, "low minstret write did not suppress retirement increment");
    step(1'b1, 1'b1, CSR_MINSTRETH, 32'hffffffff);
    assert (cycle == 4 && instret == 64'hffffffffffffffff)
      else $fatal(1, "high minstret write did not preserve the low half");
    step(1'b1, 1'b0, '0, '0);
    assert (cycle == 5 && instret == 0)
      else $fatal(1, "minstret did not carry across its 64-bit boundary");

    step(1'b0, 1'b1, CSR_MCYCLE, 32'hffffffff);
    assert (cycle == 64'h00000000ffffffff && instret == 0)
      else $fatal(1, "low mcycle write did not suppress the cycle increment");
    step(1'b0, 1'b1, CSR_MCYCLEH, 32'h12345678);
    assert (cycle == 64'h12345678ffffffff)
      else $fatal(1, "high mcycle write did not preserve the low half");
    step(1'b0, 1'b0, '0, '0);
    assert (cycle == 64'h1234567900000000)
      else $fatal(1, "mcycle did not carry across its low half");

    $display("RISC-V RV32 base counters passed");
    $finish;
  end
endmodule
