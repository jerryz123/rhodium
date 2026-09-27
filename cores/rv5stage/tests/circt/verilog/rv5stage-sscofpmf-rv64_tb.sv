// Runs integrated overflow, CSR, and interrupt tests at RV64.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_sscofpmf_rv64_tb;
  localparam int XLEN = 64;
  localparam bit HYPERVISOR = 0;
`include "cores/rv5stage/tests/circt/verilog/rv5stage-sscofpmf-body.svh"
endmodule
