// Checks data-independent integer timing in the RV64 None specialization.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_zkt_rv64_tb;
  localparam int W = 64;
`include "cores/rv5stage/tests/circt/verilog/rv5stage-zkt-body.svh"
endmodule
