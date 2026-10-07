// Runs integrated overflow, CSR, and interrupt tests at RV64.
// SPDX-License-Identifier: Apache-2.0
module riscv_sscofpmf_rv64_tb;
  localparam int XLEN = 64;
  localparam bit HYPERVISOR = 0;
`include "cores/csr/tests/circt/verilog/riscv-sscofpmf-body.svh"
endmodule
