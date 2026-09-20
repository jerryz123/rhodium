// Checks mask scans and reductions across eight VRF words, including SEW8 count/index wrap.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_vector_mask_512_tb;
  localparam int XLEN = 64, VLEN = 512;
  `include "cores/rv5stage/tests/circt/verilog/rv5stage-vector-reduction-body.svh"
endmodule
