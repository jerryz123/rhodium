// Runs the shared decoded-sequencer scoreboard at RV32 and VLEN256.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_vector_sequencer_rv32_tb;
  localparam int XLEN = 32, VLEN = 256;
  `include "cores/rv5stage/tests/circt/verilog/rv5stage-vector-sequencer-body.svh"
endmodule
