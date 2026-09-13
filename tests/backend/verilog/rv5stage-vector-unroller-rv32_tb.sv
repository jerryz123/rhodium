// Runs the shared decoded-unroller scoreboard at RV32 and VLEN256.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_vector_unroller_rv32_tb;
  localparam int XLEN = 32, VLEN = 256;
  `include "tests/backend/verilog/rv5stage-vector-unroller-body.svh"
endmodule
