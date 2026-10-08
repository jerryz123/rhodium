// Reuses the host MMIO protocol regression with independent occurrence-graph checks.
// SPDX-License-Identifier: Apache-2.0
`define FESVR_EVENT_TRACE
`include "rhodium/event/tests/circt/verilog/event-fesvr-behavior.svh"
module event_fesvr_tb;
  event_fesvr_behavior test();
endmodule
