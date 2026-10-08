// Checks fresh-S2 gshare redirects, direction overrides, straddles, replay, and retained history ownership.
// SPDX-License-Identifier: Apache-2.0
module rv2wide_bht_tb;
  typedef struct packed { logic valid; logic [63:0] cause, value; } fault_t;
  typedef struct packed { logic valid; logic [63:0] pc, target; logic compressed; logic [1:0] ras_action; } prediction_t;
  typedef struct packed { logic valid; logic [11:0] index; logic [9:0] history; logic taken; } direction_t;
  typedef struct packed { logic [63:0] pc; logic [31:0] instruction, raw_instruction; logic [63:0] sequential_pc; logic compressed_illegal; fault_t fault; prediction_t prediction; logic [1:0] speculated_ras_action; direction_t direction; } instruction_t;
  typedef struct packed { logic [1:0] count; instruction_t [1:0] entries; } packet_t;
  typedef struct packed { logic valid; packet_t bits; } packet_flow_t;
  typedef struct packed { logic [1:0] disposition; logic [63:0] cause, value; } resolution_t;
  typedef struct packed { logic [63:0] pc, target; resolution_t resolution; } redirect_t;
  typedef struct packed { logic valid; redirect_t bits; } redirect_flow_t;
  typedef struct packed { logic valid; logic [63:0] bits; } address_flow_t;
  typedef struct packed { logic valid; } pulse_t;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic [63:0] data; logic access_fault, replay; } result_t;
  typedef struct packed { logic valid; result_t bits; } result_flow_t;
  typedef struct packed { logic [63:0] address; logic cacheable, device; } fetch_request_t;
  typedef struct packed { logic valid; fetch_request_t bits; } fetch_flow_t;
  typedef struct packed { logic flush, invalidate_all, s1_kill; fetch_flow_t request; } memory_request_t;
  typedef struct packed { result_flow_t response; } memory_response_t;
  typedef struct packed { logic [63:0] address; resolution_t resolution; logic [1:0] pbmt; } translation_result_t;
  typedef struct packed { address_flow_t request; } translation_request_t;
  typedef struct packed { logic [63:0] pc, target; logic branch, conditional, taken, compressed; logic [1:0] ras_action, predicted_ras_action; logic [63:0] return_address; } update_t;
  typedef struct packed { logic valid; update_t bits; } update_flow_t;
  typedef struct packed { logic valid; logic [11:0] index; logic taken; } direction_update_t;
  typedef struct packed { logic valid; logic [9:0] bits; } history_t;
  logic clock=0, reset=1;
  address_flow_t start_in='0, virtual_lookup_out;
  redirect_flow_t redirect_in='0;
  pulse_t flush_in='0, invalidate_in='0, predictor_restore_in='0, predictor_clear_in='0;
  ready_t virtual_lookup_in='{1'b1}, instructions_in='{1'b1};
  packet_flow_t instructions_out;
  memory_request_t memory_out;
  memory_response_t memory_in;
  translation_request_t translation_out;
  translation_result_t translation_in;
  update_flow_t branch_update_in='0;
  direction_update_t direction_update_in='0;
  history_t history_restore_in='0;
  int cycles=0, scenario=0, source_cycle=-1, target_cycle=-1, branch_cycle=-1, btb_cycle=-1, checks=0, history_followups=0, recovery_packets=0;
  logic response_valid=0, response_replay=0;
  logic [63:0] response_data=0, branch_pc, target_pc;
  bit taken=1, saw_replay=0, replay_once=0;
  logic [11:0] expected_index;
  logic [9:0] expected_history=0;
  RV2WideFrontend dut(.*);
  always #5 clock=~clock;
  assign translation_in='{translation_out.request.bits,'{2'd0,64'd0,64'd0},2'd0};
  assign memory_in='{ '{response_valid,'{response_data,1'b0,response_replay}} };
  function automatic logic [63:0] word(logic [63:0] address);
    if(scenario inside {11,12}) return address=='h110 ? {32'h00100013,32'h20000063} : {32'h00100013,32'h00100013};
    if(scenario==9) return address=='h108 ? {32'h00100013,32'h20000063} : {32'h00100013,32'h00100013};
    if(address=='h100) case(scenario)
      2: return {32'h20000063,16'h0001,16'h0001};
      3,4,10: return {16'h0063,48'h000100010001};
      5: return {32'h00010001,16'hc001,16'hc001};
      default: return {32'h00100013,32'h20000063};
    endcase
    if(address=='h108 && scenario inside {3,4,10}) return {48'h000100010001,16'h2000};
    return {32'h00100013,32'h00100013};
  endfunction
  always @(posedge clock) begin
    if(reset) begin cycles=0; response_valid<=0; response_replay<=0; end
    else begin
      cycles++;
      if(cycles>150) $fatal(1,"BHT frontend timeout scenario=%0d",scenario);
      response_valid<=memory_out.request.valid && !memory_out.s1_kill && !memory_out.flush;
      response_data<=word(memory_out.request.bits.address);
      response_replay<=replay_once && !saw_replay && memory_out.request.bits.address=='h108;
      if(replay_once && memory_out.request.valid && memory_out.request.bits.address=='h108 && !memory_out.s1_kill) saw_replay=1;
      if(virtual_lookup_out.valid && virtual_lookup_in.ready) begin
        if(virtual_lookup_out.bits==(branch_pc & ~64'd7) && source_cycle<0) source_cycle=cycles;
        if(source_cycle>=0 && cycles>source_cycle && virtual_lookup_out.bits==(target_pc & ~64'd7) && target_cycle<0) target_cycle=cycles;
        if(scenario==1 && cycles==source_cycle+1 && virtual_lookup_out.bits=='h300) btb_cycle=cycles;
      end
      if(instructions_out.valid && instructions_in.ready) for(int lane=0;lane<int'(instructions_out.bits.count);lane++) begin
        instruction_t insn;
        insn=instructions_out.bits.entries[lane];
        if(insn.pc==branch_pc && branch_cycle<0) begin
          branch_cycle=cycles;
          assert(insn.direction.valid && insn.direction.index==expected_index && insn.direction.history==expected_history && insn.direction.taken==taken)
            else $fatal(1,"lost lookup/checkpoint scenario=%0d pc=%h index=%h/%h history=%h/%h",scenario,insn.pc,insn.direction.index,expected_index,insn.direction.history,expected_history);
          assert(insn.prediction.valid==taken && (!taken || insn.prediction.target==target_pc)) else $fatal(1,"effective direction mismatch");
          if(taken) assert(int'(instructions_out.bits.count)==lane+1) else $fatal(1,"taken branch did not truncate packet");
        end
        if(taken && scenario!=5 && insn.pc==target_pc) begin
          logic [9:0] after_history;
          after_history={expected_history[8:0],1'b1};
          assert(insn.direction.history==after_history && insn.direction.index==12'((((target_pc>>3)^64'(after_history))&1023)*4+((target_pc>>1)&3)))
            else $fatal(1,"history appended repeatedly under stalls or not forwarded to target lookup");
          history_followups++;
        end
        if(scenario==8 && insn.pc=='h600) begin
          assert(insn.direction.history==10'd7 && insn.direction.index==12'h31c) else $fatal(1,"recovered checkpoint was not used by S1/S2");
          recovery_packets++;
        end
      end
    end
  end
  task automatic begin_case(int which, bit want_taken=1, bit btb_taken=0, int history=0);
    @(negedge clock); reset=1; start_in='0; redirect_in='0; flush_in='0;
    branch_update_in='0; direction_update_in='0; history_restore_in='0;
    scenario=which; taken=want_taken; expected_history=10'(history);
    virtual_lookup_in.ready=0; instructions_in.ready=1;
    source_cycle=-1; target_cycle=-1; branch_cycle=-1; btb_cycle=-1; saw_replay=0; replay_once=which==4;
    branch_pc=which inside {11,12}?'h110:(which==9?'h108:(which==2?'h104:(which inside {3,4,10}?'h106:(which==5?'h102:'h100))));
    target_pc=want_taken?(which==5?branch_pc:branch_pc+'h200):((branch_pc & ~64'd7)+8);
    expected_index=12'((((branch_pc>>3)^64'(history))&1023)*4+((branch_pc>>1)&3));
    repeat(3) @(negedge clock); reset=0; start_in='{1'b1,64'h100};
    @(negedge clock); start_in='0;
    if(btb_taken) begin
      branch_update_in='{1'b1,'{branch_pc,branch_pc+'h200,1'b1,1'b1,1'b1,which==10,2'd0,2'd0,branch_pc+4}};
      @(negedge clock); branch_update_in='0;
    end
    if(want_taken) begin
      direction_update_in='{1'b1,expected_index,1'b1};
      repeat(2) @(negedge clock); direction_update_in='0;
    end
    history_restore_in='{1'b1,10'(history)};
    @(negedge clock); history_restore_in='0;
  endtask
  task automatic finish_case;
    wait(branch_cycle>=0); repeat(4) @(negedge clock);
    assert(target_cycle>=0) else $fatal(1,"direction target never requested");
    checks++;
  endtask
  initial begin
    // Miss -> taken redirects at fresh S2, even with the entire assembler stalled.
    begin_case(0); instructions_in.ready=0; virtual_lookup_in.ready=1;
    wait(target_cycle>=0); @(negedge clock);
    assert(target_cycle==source_cycle+2 && branch_cycle<0) else $fatal(1,"direction prediction depended on assembly readiness");
    repeat(6) @(negedge clock); instructions_in.ready=1; finish_case();
    // The third response exhausts block storage behind two stalled older blocks.
    // Both correction directions must save their target without overbooking it.
    for(int which=11;which<=12;which++) begin
      begin_case(which,which==11,which==12); instructions_in.ready=0; virtual_lookup_in.ready=1;
      wait(source_cycle>=0); repeat(9) @(negedge clock);
      assert(target_cycle<0 && !virtual_lookup_out.valid) else $fatal(1,"full-buffer correction bypassed block credits");
      instructions_in.ready=1; finish_case();
    end
    // An older block occupies the assembler head while the branch's fresh S2
    // outcome redirects. Predicting the queue head instead would miss this edge.
    begin_case(9); instructions_in.ready=0; virtual_lookup_in.ready=1;
    wait(target_cycle>=0); @(negedge clock);
    assert(target_cycle==source_cycle+2 && branch_cycle<0) else $fatal(1,"BHT looked at the older assembler head instead of fresh S2 data");
    repeat(6) @(negedge clock); instructions_in.ready=1; finish_case();
    // A taken S1 BTB direction is overridden to not-taken, without losing the branch.
    begin_case(1,0,1); virtual_lookup_in.ready=1; finish_case();
    assert(target_cycle==source_cycle+2 && btb_cycle==source_cycle+1) else $fatal(1,"not-taken correction was not S2");
    // A direction-only override must retain the valid BTB target. Refetch
    // without clearing predictors and observe that same provisional S1 offer.
    source_cycle=-1; target_cycle=-1; branch_cycle=-1; btb_cycle=-1;
    redirect_in='{1'b1,'{64'h100,64'h100,'{2'd0,64'd0,64'd0}}}; history_restore_in='{1'b1,10'd0};
    @(negedge clock); redirect_in='0; history_restore_in='0; finish_case();
    assert(btb_cycle==source_cycle+1) else $fatal(1,"BHT not-taken override invalidated the BTB target");
    begin_case(2); virtual_lookup_in.ready=1; finish_case();
    assert(target_cycle==source_cycle+2) else $fatal(1,"compressed prefix delayed direction prediction");
    begin_case(3); virtual_lookup_in.ready=1; finish_case();
    assert(target_cycle==source_cycle+3) else $fatal(1,"straddle direction was not predicted with its upper parcel");
    begin_case(4); virtual_lookup_in.ready=1; finish_case();
    assert(saw_replay) else $fatal(1,"continuation replay not exercised");
    // The BTB mistakes the byte-six prefix for a complete compressed branch.
    // S2 must fetch its continuation and retain the original BHT row.
    begin_case(10,1,1); virtual_lookup_in.ready=1; finish_case();
    // Both compressed branches share a row; only the second bank predicts taken.
    begin_case(5); virtual_lookup_in.ready=1; finish_case();
    begin_case(6,1,0,5); virtual_lookup_in.ready=1; finish_case();
    // Hold the S2 replacement at array admission, then verify the saved offer.
    begin_case(7); virtual_lookup_in.ready=1;
    wait(source_cycle>=0); @(negedge clock); virtual_lookup_in.ready=0;
    repeat(5) begin
      @(negedge clock);
      assert(virtual_lookup_out.valid && virtual_lookup_out.bits=='h300) else $fatal(1,"blocked gshare cursor lost");
    end
    virtual_lookup_in.ready=1; finish_case();
    // Architectural recovery outranks a simultaneous direction correction.
    begin_case(8); virtual_lookup_in.ready=1;
    wait(memory_in.response.valid); @(negedge clock);
    redirect_in='{1'b1,'{64'h100,64'h600,'{2'd0,64'd0,64'd0}}}; history_restore_in='{1'b1,10'd7}; #1;
    assert(virtual_lookup_out.valid && virtual_lookup_out.bits=='h600 && !instructions_out.valid) else $fatal(1,"architectural redirect lost priority");
    @(negedge clock); redirect_in='0; history_restore_in='0;
    wait(recovery_packets>0); @(negedge clock); checks++;
    assert(history_followups>=5) else $fatal(1,"post-prediction history not exercised");
    $display("RV2Wide S2 gshare simulation passed: %0d scenarios",checks); $finish;
  end
endmodule
