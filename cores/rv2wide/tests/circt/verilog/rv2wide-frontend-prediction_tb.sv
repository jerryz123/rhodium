// Checks same-cycle S1 prediction/S2 correction, blocked offers, and recovery through public ports.
// SPDX-License-Identifier: Apache-2.0
module rv2wide_frontend_prediction_tb;
  typedef struct packed { logic valid; logic [63:0] cause, value; logic [65:0] guest; } fault_t;
  typedef struct packed { logic valid; logic [63:0] pc, target; logic compressed; logic [1:0] ras_action; } prediction_t;
  typedef struct packed { logic valid; logic [11:0] index; logic [9:0] history; logic taken; } direction_t;
  typedef struct packed { logic [63:0] pc; logic [31:0] instruction, raw_instruction; logic [63:0] sequential_pc; logic compressed_illegal; fault_t fault; prediction_t prediction; logic [1:0] speculated_ras_action; direction_t direction; } instruction_t;
  typedef struct packed { logic [1:0] count; instruction_t [1:0] entries; } packet_t;
  typedef struct packed { logic valid; packet_t bits; } packet_flow_t;
  typedef struct packed { logic [1:0] disposition; logic [63:0] cause, value; logic [65:0] guest; } resolution_t;
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
  logic [13:0] direction_update_in='0;
  logic [68:0] ras_resolution_in='0;
  logic [4:0] history_commit_in='0;
  logic [10:0] history_restore_in='0;
  logic response_valid=0, response_error=0, response_replay=0;
  logic [63:0] response_data=0, array_address=0;
  logic array_valid=0;
  int scenario=0, cycles=0, source_cycle=-1, accepted_cycle=-1, target_cycle=-1, checks=0, packets=0;
  logic [63:0] source_pc, branch_pc, target_pc;
  bit inject_younger_error=0, inject_younger_replay=0;
  RV2WideFrontend dut(.*);
  always #5 clock=~clock;
  assign translation_in='{translation_out.request.bits, '{2'd0,64'd0,64'd0,'0},2'd0};
  // Fixed-cycle cache outcomes; S1 kill/flush suppress the next response, not
  // the current one. Frontend commands suppress current publication themselves.
  // Real L1I combinational flush behavior is covered by the production fetch test.
  assign memory_in='{ '{response_valid, '{response_data,response_error,response_replay}} };
  function automatic logic [31:0] jal(int offset);
    return {1'(offset>>20),10'(offset>>1),1'(offset>>11),8'(offset>>12),5'd0,7'h6f};
  endfunction
  function automatic logic [63:0] word(logic [63:0] address);
    if(address==64'h100) case(scenario)
      0,3,4,5,6,9,10,11,14,15: return {32'h00100013,jal('h200)};
      1: return {jal('h1fc),32'h00100013};
      2: return {32'h00100013,32'h20000063}; // BEQ x0,x0,+512.
      7: return {48'h000100010001,16'ha001}; // C.J +0.
      8: return {jal(-'h80),32'h00100013};
      12,13: return {16'h006f,48'h000100010001}; // JAL x0,+506 prefix at byte six.
      default: return {32'h00100013,32'h00100013};
    endcase
    if(address==64'h108 && (scenario==12 || scenario==13)) return {48'h000100010001,16'h1fa0};
    return {32'h00100013,32'h00100013};
  endfunction
  always @(posedge clock) begin
    if(reset) begin
      cycles=0; array_valid<=0; response_valid<=0;
    end else begin
      cycles++;
      if(virtual_lookup_out.valid && virtual_lookup_in.ready && virtual_lookup_out.bits==64'h100 && source_cycle<0) source_cycle=cycles;
      if(memory_out.request.valid && !memory_out.s1_kill && !memory_out.flush)
        assert(array_valid && memory_out.request.bits.address==array_address) else $fatal(1,"physical lookup lost its S0 owner");
      if(memory_out.request.valid && memory_out.request.bits.address==64'h100 && (scenario==2 || scenario==3 || scenario==7 || scenario==11 || scenario==14 || scenario==15) && accepted_cycle<0)
        assert(cycles==source_cycle+1 && virtual_lookup_out.valid && virtual_lookup_out.bits==((scenario==11 || scenario>=14)?target_pc:64'h700))
          else $fatal(1,"BTB result did not select a request during S1 scenario=%0d cycle=%0d source=%0d request=%h",scenario,cycles,source_cycle,virtual_lookup_out.bits);
      array_valid<=virtual_lookup_out.valid && virtual_lookup_in.ready;
      array_address<=virtual_lookup_out.bits;
      response_valid<=memory_out.request.valid && !memory_out.s1_kill && !memory_out.flush;
      response_data<=word(memory_out.request.bits.address);
      response_error<=inject_younger_error && memory_out.request.bits.address!=64'h100;
      response_replay<=inject_younger_replay && memory_out.request.bits.address!=64'h100;
      if(virtual_lookup_out.valid && virtual_lookup_in.ready && virtual_lookup_out.bits==(target_pc & ~64'd7) && cycles>source_cycle && target_cycle<0)
        target_cycle=cycles;
      if(instructions_out.valid && instructions_in.ready) begin
        packets++;
        for(int lane=0;lane<int'(instructions_out.bits.count);lane++) begin
          instruction_t insn;
          insn=instructions_out.bits.entries[lane];
          if(insn.pc==branch_pc && accepted_cycle<0) begin
            accepted_cycle=cycles;
            assert(insn.prediction.valid && insn.prediction.target==target_pc && !insn.fault.valid && int'(instructions_out.bits.count)==lane+1)
              else $fatal(1,"accepted prefix/target mismatch scenario=%0d pc=%h predicted=%b target=%h expected=%h fault=%b count=%0d lane=%0d",scenario,insn.pc,insn.prediction.valid,insn.prediction.target,target_pc,insn.fault.valid,instructions_out.bits.count,lane);
            assert(memory_out.s1_kill==(scenario!=11 && scenario<14) && !memory_out.flush) else $fatal(1,"repair suppressed its own outcome, retained younger lookup, or repaired a correct target");
          end
          if(accepted_cycle>=0 && cycles>accepted_cycle && insn.pc!=target_pc && insn.pc<64'h300 && scenario!=7 && scenario!=8)
            $fatal(1,"younger suffix escaped correction: %h",insn.pc);
        end
      end
    end
  end
  task automatic restart(int test_case, bit predicted=0);
    @(negedge clock); reset=1; start_in='0; redirect_in='0; branch_update_in='0;
    instructions_in.ready=1; virtual_lookup_in.ready=1;
    inject_younger_error=0; inject_younger_replay=0;
    source_cycle=-1; accepted_cycle=-1; target_cycle=-1; packets=0; scenario=test_case;
    source_pc='h100; branch_pc=(test_case==1 || test_case==8)?'h104:'h100;
    if(test_case==12 || test_case==13) branch_pc='h106;
    target_pc=test_case==7?'h100:(test_case==8?'h84:'h300);
    repeat(3) @(negedge clock); reset=0;
    if(predicted || test_case==15) virtual_lookup_in.ready=0;
    start_in='{1'b1,source_pc};
    @(negedge clock); start_in='0;
    if(predicted) begin
      // Deliberately stale BTB target; S2 must retain its taken direction but
      // replace this target with the instruction's immediate. Train after
      // start's predictor clear, while array admission holds the cursor.
      branch_update_in='{1'b1,'{branch_pc,(test_case==11 || test_case==14)?target_pc:64'h700,1'b1,test_case==2,1'b1,test_case==7,2'd0,2'd0,branch_pc+(test_case==7?2:4)}};
      @(negedge clock); branch_update_in='0; virtual_lookup_in.ready=1;
    end else if(test_case==15) begin
      // Train at the S0 admission edge. Only an S1 lookup can see this entry
      // and offer its successor before the next edge; an S0 lookup is too early.
      virtual_lookup_in.ready=1;
      branch_update_in='{1'b1,'{branch_pc,target_pc,1'b1,1'b0,1'b1,1'b0,2'd0,2'd0,branch_pc+4}};
      @(negedge clock); branch_update_in='0;
    end
  endtask
  task automatic check_repair_request;
    wait(accepted_cycle>=0);
    @(negedge clock);
    assert(target_cycle==accepted_cycle && !memory_out.flush)
      else $fatal(1,"S2 target not accepted on the repair edge scenario=%0d accepted=%0d target=%0d",scenario,accepted_cycle,target_cycle);
    checks++;
  endtask
  initial begin
    for(int test_case=0;test_case<3;test_case++) begin
      restart(test_case,test_case==2); check_repair_request();
    end
    restart(3,1); check_repair_request();
    restart(4); instructions_in.ready=0; inject_younger_error=1;
    repeat(8) @(negedge clock);
    assert(accepted_cycle<0 && packets==0) else $fatal(1,"blocked packet repaired early");
    // The branch is now in returned-block storage. Its correction must outrank
    // the younger S2 error and restart stopped fetch without publishing that error.
    repeat(2) @(negedge clock);
    instructions_in.ready=1; check_repair_request();
    restart(5); instructions_in.ready=0;
    wait(instructions_out.valid); @(negedge clock);
    virtual_lookup_in.ready=0; instructions_in.ready=1;
    wait(accepted_cycle>=0);
    repeat(3) begin
      @(negedge clock);
      assert(virtual_lookup_out.valid && virtual_lookup_out.bits==(target_pc & ~64'd7) && target_cycle<0) else $fatal(1,"blocked corrected cursor lost");
    end
    virtual_lookup_in.ready=1; @(negedge clock); checks++;
    restart(6);
    wait(instructions_out.valid);
    @(negedge clock); redirect_in='{1'b1,'{64'h100,64'h600,'{2'd0,64'd0,64'd0,'0}}};
    #1;
    assert(accepted_cycle<0 && virtual_lookup_out.valid && virtual_lookup_out.bits==64'h600) else $fatal(1,"architectural redirect lost priority accepted=%0d valid=%b target=%h",accepted_cycle,virtual_lookup_out.valid,virtual_lookup_out.bits);
    @(negedge clock); redirect_in='0;
    checks++;
    restart(7,1); check_repair_request();
    restart(8); check_repair_request();
    for(int test_case=9;test_case<=10;test_case++) begin
      restart(test_case); instructions_in.ready=0;
      inject_younger_error=test_case==9; inject_younger_replay=test_case==10;
      wait(memory_in.response.valid && (memory_in.response.bits.access_fault || memory_in.response.bits.replay));
      @(negedge clock); instructions_in.ready=1;
      check_repair_request();
    end
    restart(11,1);
    wait(accepted_cycle>=0); @(negedge clock);
    assert(target_cycle==source_cycle+1 && target_cycle<accepted_cycle && !memory_out.flush) else $fatal(1,"correct S1 prediction was slowed by S2");
    checks++;
    restart(12); check_repair_request();
    restart(13,1); check_repair_request();
    restart(14,1); instructions_in.ready=0;
    wait(source_cycle>=0); @(negedge clock); virtual_lookup_in.ready=0;
    @(negedge clock);
    // The S1 result must be retained after S1 drains, even if training changes
    // that entry while the successor is blocked. Its S2 metadata stays original.
    branch_update_in='{1'b1,'{branch_pc,64'h700,1'b1,1'b0,1'b1,1'b0,2'd0,2'd0,branch_pc+4}};
    @(negedge clock); branch_update_in='0;
    repeat(3) begin
      #1;
      assert(virtual_lookup_out.valid && virtual_lookup_out.bits==target_pc && target_cycle<0) else $fatal(1,"blocked S1 prediction was not retained");
      @(negedge clock);
    end
    virtual_lookup_in.ready=1; instructions_in.ready=1;
    wait(accepted_cycle>=0); @(negedge clock);
    assert(target_cycle==accepted_cycle) else $fatal(1,"retained S1 target did not drain");
    checks++;
    restart(15);
    wait(accepted_cycle>=0); @(negedge clock);
    assert(target_cycle==source_cycle+1 && target_cycle<accepted_cycle) else $fatal(1,"lookup used predictor state from S0 instead of S1");
    checks++;
    $display("RV2Wide S1 prediction/S2 correction timing passed (%0d checks)",checks); $finish;
  end
  initial begin #20000; $fatal(1,"frontend redirect timeout scenario=%0d",scenario); end
endmodule
