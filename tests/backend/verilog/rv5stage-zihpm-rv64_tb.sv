// Runs the shared zero-valued Zihpm contract at XLEN=64.
module rv5stage_zihpm_rv64_tb;
  localparam int XLEN = 64;
`include "tests/backend/verilog/rv5stage-zihpm-body.svh"
endmodule
