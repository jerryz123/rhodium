// Runs the shared decoded-sequencer scoreboard at RV64 and VLEN128.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_vector_sequencer_tb;
  localparam int XLEN = 64, VLEN = 128;
  `include "cores/rv5stage/tests/circt/verilog/rv5stage-vector-sequencer-body.svh"
endmodule
