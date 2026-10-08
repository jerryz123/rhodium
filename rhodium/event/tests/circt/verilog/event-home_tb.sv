// Runs the inclusive Home regression with exact event-graph checks on public transfers.
// SPDX-License-Identifier: Apache-2.0
`define CHI_HOME_TRACE
`include "rhodium/event/tests/circt/verilog/event-home-behavior.svh"
module event_home_tb;
  event_home_behavior test();
endmodule
