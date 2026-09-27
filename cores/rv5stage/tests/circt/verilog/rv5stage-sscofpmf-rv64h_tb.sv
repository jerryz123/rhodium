// Runs integrated overflow, CSR, and interrupt tests at RV64 with H.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_sscofpmf_rv64h_tb;
  localparam int XLEN = 64;
  localparam bit HYPERVISOR = 1;
`define RHODIUM_HPM_TEST_H
`include "cores/rv5stage/tests/circt/verilog/rv5stage-sscofpmf-body.svh"
`undef RHODIUM_HPM_TEST_H
endmodule
