// Checks RV32 reductions, sign extension, truncation, and VLEN256 storage through public transactions.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_vector_reduction_rv32_tb;
  localparam int XLEN = 32, VLEN = 256;
  `include "tests/backend/verilog/rv5stage-vector-reduction-body.svh"
endmodule
