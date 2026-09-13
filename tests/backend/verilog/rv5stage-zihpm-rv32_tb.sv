// Runs the shared zero-valued Zihpm contract at XLEN=32.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_zihpm_rv32_tb;
  localparam int XLEN = 32;
`include "tests/backend/verilog/rv5stage-zihpm-body.svh"
endmodule
