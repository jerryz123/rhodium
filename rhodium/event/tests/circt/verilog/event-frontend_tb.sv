// Runs the public compressed-fetch regression against compiler-instrumented RTL.
// SPDX-License-Identifier: Apache-2.0
`define RV5STAGE_FETCH_TRACE
`include "cores/rv5stage/tests/circt/verilog/rv5stage-fetch_tb.sv"
