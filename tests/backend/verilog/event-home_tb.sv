// Runs the inclusive Home regression with exact event-graph checks on public transfers.
// SPDX-License-Identifier: Apache-2.0
`define CHI_HOME_TRACE
`include "tests/backend/verilog/chi-inclusive-home_tb.sv"
module event_home_tb;
  chi_inclusive_home_tb test();
endmodule
