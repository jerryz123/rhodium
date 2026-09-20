// Models the packed memory-writeback union independently at the RTL test boundary.
// SPDX-License-Identifier: Apache-2.0
`ifndef RV5STAGE_MEMORY_WRITEBACK_SVH
`define RV5STAGE_MEMORY_WRITEBACK_SVH
function automatic logic [8:0] memory_integer(input logic [4:0] rd);
  return {2'd1, 2'b0, rd};
endfunction
function automatic logic [8:0] memory_fp(input logic [4:0] rd, input logic [1:0] precision);
  return {2'd2, rd, precision};
endfunction
function automatic logic [8:0] memory_vector(input logic [2:0] slot);
  return {2'd3, 4'b0, slot};
endfunction
function automatic logic [4:0] memory_rd(input logic [8:0] writeback);
  return writeback[8:7] == 2'd2 ? writeback[6:2] : writeback[4:0];
endfunction
`endif
