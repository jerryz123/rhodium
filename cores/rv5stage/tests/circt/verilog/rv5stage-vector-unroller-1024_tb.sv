// Runs the decoded-unroller scoreboard with valid gather indices above 255.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_vector_unroller_1024_tb;
  localparam int XLEN = 64, VLEN = 1024;
  `include "cores/rv5stage/tests/circt/verilog/rv5stage-vector-unroller-body.svh"
endmodule
