// Checks RV64 reductions and scalar moves at the production pipeline's public transaction boundaries.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_vector_reduction_tb;
  localparam int XLEN = 64, VLEN = 128;
  `include "cores/rv5stage/tests/circt/verilog/rv5stage-vector-reduction-body.svh"
endmodule
