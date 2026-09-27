// Runs the RV32 non-hypervisor HPM contract against its public ports.
// SPDX-License-Identifier: Apache-2.0
module riscv_hpm_rv32_tb;
  localparam int XLEN = 32;
  localparam bit HYPERVISOR = 0;
  `include "riscv/rtl/tests/circt/verilog/riscv-hpm-body.svh"
endmodule
