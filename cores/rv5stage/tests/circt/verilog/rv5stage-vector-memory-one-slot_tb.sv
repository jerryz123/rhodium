// Runs vector memory ordering, masks, and fault restart with one reusable completion slot.
// SPDX-License-Identifier: Apache-2.0
`define RV5STAGE_VECTOR_COMPLETION_SLOTS 1
`include "cores/rv5stage/tests/circt/verilog/rv5stage-vector-memory_tb.sv"
