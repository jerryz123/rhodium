// Runs the shared zero-valued Zihpm contract at XLEN=64.
// SPDX-License-Identifier: Apache-2.0
module riscv_zihpm_rv64_tb;
  localparam int XLEN = 64;
`include "cores/csr/tests/circt/verilog/riscv-zihpm-body.svh"
endmodule
