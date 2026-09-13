// Runs the shared decoded-unroller scoreboard at RV64 and VLEN128.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_vector_unroller_tb;
  localparam int XLEN = 64, VLEN = 128;
  `include "tests/backend/verilog/rv5stage-vector-unroller-body.svh"
endmodule
