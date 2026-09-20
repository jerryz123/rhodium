// Drives equal-PC vector macros through stalls, retries, faults, and reordered memory responses.
// SPDX-License-Identifier: Apache-2.0
module event_vector_tb;
`ifdef VECTOR_ONE_SLOT
  localparam int TAG_BITS=1;
`else
  localparam int TAG_BITS=2;
`endif
  logic clock=0, reset=1;
  logic [31:0] instruction=0;
  logic [63:0] vl=0, vstart=0, vtype=24;
  logic request_valid=0, issue_ready=1, cancel=0, slow=0, response_valid=0;
  logic [1:0] disposition=0;
  logic [TAG_BITS-1:0] response_tag=0;
  wire request_ready, active, issued, committed, last, enabled, memory;
  wire [TAG_BITS-1:0] issue_tag;
  RV5StageVectorTrace dut(.*);
  always #5 clock=~clock;
  import "DPI-C" function void vector_trace_bind();
  import "DPI-C" function int unsigned vector_trace_response();
  import "DPI-C" function void vector_trace_sample(input int unsigned rst, launch, insn, length,
    issue, tag, mem, enable, commit, status, delayed, response, response_tag, cancel);
  import "DPI-C" function void vector_trace_check();
  import "DPI-C" function void vector_trace_finish();
  bit sampled_launch, sampled_commit;
  task automatic tick;
    @(posedge clock);
    sampled_launch=request_valid && request_ready;
    sampled_commit=committed;
    vector_trace_sample(32'(reset),32'(sampled_launch),instruction,32'(vl),32'(issued),
      32'(issue_tag),32'(memory),32'(enabled),32'(committed),32'(disposition),32'(slow),
      32'(response_valid),32'(response_tag),32'(cancel));
    #1; vector_trace_check();
    @(negedge clock);
  endtask
  task automatic responses;
    int unsigned result;
    result=vector_trace_response();
    response_valid=result[8]; response_tag=result[TAG_BITS-1:0];
  endtask
  task automatic run_macro(input logic [31:0] insn, input int length,
      input bit delayed=0, input int recovery=0, input bit reset_tail=0);
    int accepted;
    bit recovered, finished;
    instruction=insn; vl=64'(length); request_valid=1;
    slow=delayed; accepted=0; recovered=0; finished=0;
    disposition=0; issue_ready=1; response_valid=0;
    do tick(); while(!sampled_launch);
    request_valid=0;
    for(int step=0;step<600 && !finished;++step) begin
      issue_ready=step%7!=2 && step%7!=3;
      disposition=(!recovered && recovery>0 && recovery<4 && accepted==1) ? 2'(recovery) : 0;
      cancel=!recovered && recovery==4 && accepted==1;
      responses();
      tick();
      if(cancel) recovered=1;
      if(sampled_commit) begin
        if(disposition!=0) recovered=1;
        else ++accepted;
      end
      if(reset_tail && accepted==2) begin
        reset=1; response_valid=0; disposition=0; tick(); reset=0; finished=1;
      end else if(!active) finished=1;
    end
    assert(finished) else $fatal(1,"vector macro did not drain");
    disposition=0; response_valid=0; issue_ready=1; cancel=0;
    repeat(4) tick();
  endtask
  initial begin
    vector_trace_bind(); tick(); reset=0;
    // E64 m8 makes packed integer and memory beats both singleton elements.
    vtype=27;
    run_macro(32'h02800457,16); // vadd.vv v8,v8,v0
    run_macro(32'h02800457,16,0,1); // Same PC and instruction, retry after a prefix.
    run_macro(32'h02800457,0); // Empty completion has no write.
    run_macro(32'h02007407,16,1); // vle64.v v8,(x0), reordered slow completions and slot wrap.
    run_macro(32'h02007407,8,1,1); // Retry preserves accepted older slots.
    run_macro(32'h02007407,8,1,2); // Fault also preserves accepted older slots.
    run_macro(32'h02007407,8,1,4); // Cancellation drops only unaccepted work.
    run_macro(32'h03007407,8,0,3); // Fault-only-first: truncate after the older hit has drained.
    run_macro(32'h02007427,8); // Stores complete without a VRF write.
    run_macro(32'h02007407,8,1,0,1); // Reset with accepted work and speculative beats.
    run_macro(32'h02800457,16); // New epoch reuses all occurrence counters.
    vtype=24;
    run_macro(32'h5e003057,2); // vmv.v.i v0,0 initializes the mask through real writes.
    vtype=27;
    run_macro(32'h00800457,8); // Fully masked vadd still has no-write completions.
    vector_trace_finish(); $finish;
  end
endmodule
