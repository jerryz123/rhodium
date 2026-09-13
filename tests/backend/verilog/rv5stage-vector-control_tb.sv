// Runs the RV64 VLEN=128 vector configuration and CSR contract.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_vector_control_tb;
  localparam integer XLEN = 64, VLEN = 128;
`include "tests/backend/verilog/rv5stage-vector-control-body.svh"
endmodule
