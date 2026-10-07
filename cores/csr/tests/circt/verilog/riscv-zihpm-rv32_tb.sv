// Runs the shared zero-valued Zihpm contract at XLEN=32.
// SPDX-License-Identifier: Apache-2.0
module riscv_zihpm_rv32_tb;
  localparam int XLEN = 32;
`include "cores/csr/tests/circt/verilog/riscv-zihpm-body.svh"
endmodule
