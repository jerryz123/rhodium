// Runs the decoded-sequencer scoreboard with valid gather indices above 255.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_vector_sequencer_1024_tb;
  localparam int XLEN = 64, VLEN = 1024;
  `include "cores/rv5stage/tests/circt/verilog/rv5stage-vector-sequencer-body.svh"
endmodule
