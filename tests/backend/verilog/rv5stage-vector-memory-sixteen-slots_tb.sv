// Runs vector memory ordering, overlap, and fault restart with sixteen completion slots.
// SPDX-License-Identifier: Apache-2.0
`define RV5STAGE_VECTOR_COMPLETION_SLOTS 16
`include "tests/backend/verilog/rv5stage-vector-memory_tb.sv"
