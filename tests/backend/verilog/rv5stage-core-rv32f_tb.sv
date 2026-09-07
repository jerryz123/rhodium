// Runs the architectural WB authorization scenarios on RV32F.
module rv5stage_core_rv32f_tb;
  localparam int XLEN = 32;
  `include "tests/backend/verilog/rv5stage-core-wb-body.svh"
endmodule
