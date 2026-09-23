// Checks contested LSU lookup, retained store ownership, fault routing, and delayed tagged responses.
// SPDX-License-Identifier: Apache-2.0
`include "cores/rv5stage/tests/circt/verilog/rv5stage-memory-writeback.svh"
module rv5stage_memory_arbiter_tb;
  typedef struct packed { logic [7:0] byte_mask; logic [63:0] address; logic [3:0] access, atomic; logic [1:0] width; logic unsigned_0; logic [63:0] data; logic [8:0] writeback; logic origin; logic [2:0] locality; } request_t;
  typedef struct packed { logic valid; request_t bits; } request_flow_t;
  typedef struct packed { request_flow_t request; } data_request_t;
  typedef struct packed { logic access_fault; logic [63:0] data; logic [8:0] writeback; logic origin; } response_t;
  typedef struct packed { logic valid; response_t bits; } response_flow_t;
  typedef struct packed { logic request_ready, request_fault, request_access_fault; response_flow_t response; logic drained, reservation_valid; } data_response_t;
  typedef struct packed { logic [7:0] byte_mask; logic [63:0] address; logic [3:0] access; logic [1:0] width; logic unsigned_0; logic [63:0] data; } lookup_t;
  typedef struct packed { logic valid; lookup_t bits; } lookup_flow_t;
  typedef struct packed { lookup_flow_t request; logic commit; } pipeline_request_t;
  typedef struct packed { logic [2:0] outcome, reason; logic [63:0] data; } result_t;
  typedef struct packed { logic valid; result_t bits; } result_flow_t;
  typedef struct packed { result_flow_t response; logic commit_ready; } pipeline_response_t;
  logic clock = 0, reset = 1;
  data_request_t scalar_in, vector_in, memory_out;
  data_response_t scalar_out, vector_out, memory_in;
  pipeline_request_t scalar_pipeline_in, vector_pipeline_in, pipeline_out;
  pipeline_response_t scalar_pipeline_out, vector_pipeline_out, pipeline_in;
  RV5StageMemoryArbiter dut(.pipeline_vector(), .*);
  always #5 clock = ~clock;
  task automatic tick;
    @(posedge clock); #1;
  endtask
  initial begin
    scalar_in = '0; vector_in = '0; memory_in = '0;
    scalar_pipeline_in = '0; vector_pipeline_in = '0; pipeline_in = '0;
    tick(); tick(); @(negedge clock); reset = 0;

    // Both attempt a lookup. Scalar wins; vector receives replay one cycle later.
    scalar_pipeline_in.request = '{valid:1, bits:'{byte_mask:8'(((1 << (1 << (2'd3))) - 1) << ((64'h100) % 8)),address:64'h100, access:4'd2, width:2'd3, unsigned_0:1, data:64'h11}};
    vector_pipeline_in.request = '{valid:1, bits:'{byte_mask:8'(((1 << (1 << (2'd3))) - 1) << ((64'h200) % 8)),address:64'h200, access:4'd2, width:2'd3, unsigned_0:1, data:64'h22}};
    #1; assert(pipeline_out.request.valid && pipeline_out.request.bits.address == 'h100) else $fatal(1,"lookup priority");
    tick();
    pipeline_in.response = '{valid:1, bits:'{outcome:3'd2, reason:0, data:64'h11}};
    #1;
    assert(scalar_pipeline_out.response.valid && scalar_pipeline_out.response.bits.outcome == 2) else $fatal(1,"scalar lookup result");
    assert(vector_pipeline_out.response.valid && vector_pipeline_out.response.bits.outcome == 3) else $fatal(1,"loser must replay");
    @(negedge clock); scalar_pipeline_in.request.valid = 0;
    // The next lookup belongs to vector while scalar's store reaches commit.
    tick(); pipeline_in.commit_ready = 1;
    scalar_pipeline_in.commit = 1; vector_pipeline_in.commit = 0;
    #1; assert(pipeline_out.commit && scalar_pipeline_out.commit_ready && !vector_pipeline_out.commit_ready) else $fatal(1,"retained scalar commit owner");
    assert(vector_pipeline_out.response.valid && vector_pipeline_out.response.bits.outcome == 2) else $fatal(1,"vector lookup result");
    @(negedge clock); vector_pipeline_in.request.valid = 0;
    tick(); scalar_pipeline_in.commit = 0; vector_pipeline_in.commit = 1;
    #1; assert(pipeline_out.commit && vector_pipeline_out.commit_ready && !scalar_pipeline_out.commit_ready) else $fatal(1,"retained vector commit owner");
    @(negedge clock); vector_pipeline_in.commit = 0;
    tick(); #1; assert(!pipeline_out.commit && !scalar_pipeline_out.commit_ready && !vector_pipeline_out.commit_ready) else $fatal(1,"bubble has no commit owner");

    // A test-only absent pipeline responder means Slow, never a fabricated hit.
    @(negedge clock); vector_pipeline_in.request.valid = 1; pipeline_in.response.valid = 0;
    tick(); #1; assert(vector_pipeline_out.response.valid && vector_pipeline_out.response.bits.outcome == 0) else $fatal(1,"absent lookup response");
    @(negedge clock); vector_pipeline_in.request.valid = 0;

    scalar_in.request = '{valid:1, bits:'0}; scalar_in.request.bits.address = 'h300;
    vector_in.request = '{valid:1, bits:'0}; vector_in.request.bits.address = 'h400;
    memory_in.request_ready = 0; memory_in.request_fault = 1;
    #1; assert(memory_out.request.valid && memory_out.request.bits.address == 'h300 && scalar_out.request_fault && !vector_out.request_fault && !vector_out.request_ready) else $fatal(1,"stalled scalar fault owner");
    scalar_in.request.valid = 0; memory_in.request_fault = 0; memory_in.request_access_fault = 1;
    #1; assert(memory_out.request.bits.address == 'h400 && vector_out.request_access_fault && !scalar_out.request_access_fault) else $fatal(1,"vector fault owner");
    memory_in.request_access_fault = 0; memory_in.request_ready = 1;
    #1; assert(vector_out.request_ready && !scalar_out.request_ready) else $fatal(1,"vector transfer owner");
    tick(); @(negedge clock); vector_in.request.valid = 0;
    // Response ownership comes from the accepted tag, not the current arbiter winner.
    memory_in.response = '{valid:1, bits:'{access_fault:0, data:64'h5678, writeback:memory_vector(3), origin:0}};
    #1; assert(vector_out.response.valid && !scalar_out.response.valid && vector_out.response.bits.data == 'h5678) else $fatal(1,"delayed vector completion");
    memory_in.response.bits.writeback = memory_integer(9);
    #1; assert(scalar_out.response.valid && !vector_out.response.valid) else $fatal(1,"scalar completion route");
    memory_in.drained = 1; memory_in.reservation_valid = 1;
    #1; assert(scalar_out.drained && vector_out.drained && scalar_out.reservation_valid && vector_out.reservation_valid) else $fatal(1,"memory status");
    $display("Scalar/vector LSU arbitration, replay, commit ownership, and response routing passed");
    $finish;
  end
endmodule
