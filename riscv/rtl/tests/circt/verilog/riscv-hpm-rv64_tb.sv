// Runs the RV64 guest-aware HPM contract against its public ports.
// SPDX-License-Identifier: Apache-2.0
module riscv_hpm_rv64_tb;
  localparam int XLEN = 64;
  localparam bit HYPERVISOR = 1;
  `include "riscv/rtl/tests/circt/verilog/riscv-hpm-body.svh"
endmodule
