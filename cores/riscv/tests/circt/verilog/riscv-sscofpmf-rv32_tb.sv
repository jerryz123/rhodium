// Runs integrated overflow, CSR, and interrupt tests at RV32.
// SPDX-License-Identifier: Apache-2.0
module riscv_sscofpmf_rv32_tb;
  localparam int XLEN = 32;
  localparam bit HYPERVISOR = 0;
`include "cores/riscv/tests/circt/verilog/riscv-sscofpmf-body.svh"
endmodule
