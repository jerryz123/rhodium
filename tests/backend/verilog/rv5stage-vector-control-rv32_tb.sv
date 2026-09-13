// Runs the RV32 VLEN=256 vector configuration and CSR contract.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_vector_control_rv32_tb;
  localparam integer XLEN = 32, VLEN = 256;
`include "tests/backend/verilog/rv5stage-vector-control-body.svh"
endmodule
