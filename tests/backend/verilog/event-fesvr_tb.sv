// Reuses the host MMIO protocol regression with independent occurrence-graph checks.
// SPDX-License-Identifier: Apache-2.0
`define FESVR_EVENT_TRACE
`include "tests/backend/verilog/fesvr-mmio_tb.sv"
module event_fesvr_tb;
  fesvr_mmio_tb test();
endmodule
