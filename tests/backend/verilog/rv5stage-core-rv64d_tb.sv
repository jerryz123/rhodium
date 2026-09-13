// Runs the architectural WB authorization scenarios on RV64D.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_core_rv64d_tb;
  localparam int XLEN = 64;
  `include "tests/backend/verilog/rv5stage-core-wb-body.svh"
endmodule
