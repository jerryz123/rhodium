// Checks dual-issue writes, AUIPC/ADDI pairs, paired memory/branch operands, and precise recovery.
// Checks RV2Wide architectural ordering, scheduled multiply returns, and retained memory ownership.
// SPDX-License-Identifier: Apache-2.0
module rv2wide_core_tb;
  typedef struct packed { logic [63:0] cause, value; logic [65:0] guest; } fetch_fault_t;
  typedef struct packed { logic valid; fetch_fault_t bits; } fetch_fault_flow_t;
  typedef struct packed { logic valid; logic [63:0] pc, target; logic compressed; logic [1:0] ras_action; } prediction_t;
  typedef struct packed { logic [63:0] pc, target; logic branch, conditional, taken, compressed; logic [1:0] ras_action, predicted_ras_action; logic [63:0] return_address; } branch_update_t;
  typedef struct packed { logic valid; branch_update_t bits; } branch_update_flow_t;
  typedef struct packed { logic valid; logic [11:0] index; logic [9:0] history; logic taken; } direction_t;
  typedef struct packed { logic [63:0] pc; logic [31:0] instruction, raw_instruction; logic [63:0] sequential_pc; logic compressed_illegal; fetch_fault_flow_t fault; prediction_t prediction; logic [1:0] speculated_ras_action; direction_t direction; } instruction_t;
  typedef struct packed { logic [1:0] count; instruction_t [1:0] entries; } packet_t;
  typedef struct packed { logic valid; packet_t bits; } packet_flow_t;
  typedef struct packed { logic [1:0] disposition; logic [63:0] cause, value; logic [65:0] guest; } resolution_t;
  typedef struct packed { logic valid; resolution_t bits; } resolution_flow_t;
  typedef struct packed { logic valid; instruction_t bits; } instruction_flow_t;
  typedef struct packed { instruction_t fetched; logic [4:0] rd; logic write; logic [63:0] data; logic deferred; } retirement_t;
  typedef struct packed { logic valid; retirement_t bits; } retirement_flow_t;
  typedef struct packed { logic [63:0] pc, target; resolution_t resolution; } redirect_t;
  typedef struct packed { logic valid; redirect_t bits; } redirect_flow_t;
  typedef struct packed { logic [63:0] address; logic [3:0] access, atomic; logic [1:0] width; logic [63:0] data; logic [7:0] mask; logic [2:0] locality; logic [1:0] pbmt, guest_access; } memory_req_t;
  typedef struct packed { logic valid; memory_req_t bits; } memory_req_flow_t;
  typedef struct packed { logic valid; logic [63:0] bits; } memory_resp_flow_t;
  typedef struct packed { logic request_ready; resolution_flow_t fault; memory_resp_flow_t response; logic drained, ordered_busy, reservation_valid; } memory_in_t;
  typedef struct packed { memory_req_flow_t request; logic response_ready; } memory_out_t;
  typedef struct packed { logic [2:0] outcome; logic [63:0] data; logic [65:0] guest; } lookup_t;
  typedef struct packed { logic valid; lookup_t bits; } lookup_flow_t;
  typedef struct packed { lookup_flow_t response; logic commit_ready; } pipeline_in_t;
  typedef struct packed { memory_req_flow_t request; logic commit; } pipeline_out_t;
  typedef struct packed { logic [7:0] byte_mask; logic [63:0] address; logic [3:0] access, atomic; logic [1:0] width; logic unsigned_load; logic [63:0] data; logic context_bit; logic [2:0] locality; } split_req_t;
  typedef struct packed { logic access_fault; logic [63:0] data; logic context_bit; } split_physical_resp_t;
  typedef struct packed { split_physical_resp_t response; logic page_fault; logic [63:0] fault_address; logic [66:0] guest; } split_result_t;
  typedef struct packed { logic valid; split_req_t bits; } split_request_flow_t;
  typedef struct packed { logic valid; split_result_t bits; } split_response_flow_t;
  typedef struct packed { logic request_ready; split_response_flow_t response; } split_in_t;
  split_in_t split_in='0;
  split_request_flow_t split_out;
  bit split_active=0, hold_split=0, split_fault=0;
  int split_due=0, split_requests=0;
  logic [2:0] expected_split_locality=0;
  split_result_t split_reply;

  logic clock = 0, reset = 1;
  packet_flow_t instructions;
  logic ready;
  resolution_flow_t resolution[2];
  instruction_flow_t memory_stage[2];
  retirement_flow_t retired[2];
  redirect_flow_t redirect;
  logic [1:0] issued, retired_count;
  logic [3:0] instruction_capacity;
  logic fetch_flush;
  logic [5:0] interrupts = 0;
  logic [63:0] time_counter = 123;
  localparam logic [63:0] timer_compare = 64'h10000007b;
  logic sleeping;
  bit reservation_valid=0;
  logic [63:0] trap_target = 0;
  int mem_branch_redirects = 0, wb_overrides = 0;
  int minimum_instruction_capacity = 8;
  logic inject_enable;
  logic [63:0] inject_pc;
  resolution_t inject_result;
  retirement_t expected[$];
  redirect_t expected_redirects[$];
  logic [63:0] model[32];
  integer cycles = 0, commits = 0, dual_commits = 0, single_issues = 0, stops = 0;
  integer dual_run = 0, longest_dual_run = 0;
  int pause_commits=0, pause_dual=0, pause_cycle=0;
  bit saw_repacked = 0;
  memory_in_t memory_in;
  memory_out_t memory_out;
  pipeline_in_t pipeline_in;
  pipeline_out_t pipeline_out;
  retirement_flow_t completed;
  lookup_flow_t lookup_response;
  memory_req_t lookup_request, store_candidate;
  memory_req_t expected_requests[$];
  retirement_t expected_completions[$], response_owners[$];
  logic [63:0] response_data[16];
  int response_due[16], response_read = 0, response_write = 0, response_count = 0;
  byte unsigned memory_bytes[4096], model_bytes[4096];
  bit block_requests = 0, block_stores = 0, hold_responses = 0;
  bit inject_memory_fault = 0, store_candidate_valid = 0;
  logic [63:0] fault_address;
  int configured_delay = 8, lookup_mode = 0;
  int requests = 0, responses = 0, canceled = 0, stores = 0, stall_cycles = 0, hits = 0, lookups = 0;
  int overlap_retirements = 0, max_outstanding = 0, shared_writes = 0, reserved_slots = 0;
  logic instruction_invalidate;
  logic translation_flush;
  int translation_invalidations=0;
  branch_update_flow_t branch_update;
  int branch_updates=0;
  int invalidations=0;
  int multiply_mem_cycle=-1, dependent_mem_cycle=-1;
  int multiply_authorized_cycle[logic [63:0]];
  int multiply_stream_cycle=-1, multiply_stream_count=0;
  int conditional_dual=0, mop_dual=0;
  int waw_dual=0, waw_deferred=0;
  int address_pairs=0;
  int auipc_addi_pairs=0;
  int guest_address_pairs=0;
  bit response_management[16];
  int store_data_pairs=0;
  int branch_pairs=0;
  int dual_branches=0, ras_resolutions=0, max_training_pending=0;
  logic [63:0] training_pending[$];
  logic predictor_clear;
  bit expected_branch_taken[logic [63:0]];
  typedef struct packed { logic [63:0] address; logic [1:0] operation; } prefetch_t;
  typedef struct packed { logic valid; prefetch_t bits; } prefetch_flow_t;
  prefetch_flow_t prefetch;
  prefetch_t expected_prefetch[logic [63:0]];
  int prefetch_count=0, prefetch_pairs=0;

  function automatic bit prefetch_encoding(logic [31:0] word);
    return word[6:0]==7'h13 && word[14:12]==6 && word[11:7]==0 && word[24:20] inside {0,1,3};
  endfunction
  function automatic logic [31:0] prefetch_insn(int operation, rs1, offset=0);
    return {7'(offset>>5),5'(operation),5'(rs1),3'd6,5'd0,7'h13};
  endfunction

  function automatic bit mop_encoding(logic [31:0] word);
    return (word & 32'hb3c0707f)==32'h81c04073 || (word & 32'hb200707f)==32'h82004073;
  endfunction
  function automatic bit conditional_encoding(logic [31:0] word);
    return word[6:0]==7'h33 && word[31:25]==7'h07 && word[14:12] inside {3'd5,3'd7};
  endfunction
  function automatic bit serializing_encoding(logic [31:0] word);
    return word[6:0]==7'h73 && !mop_encoding(word) && !(word[31:28]==4'h6 && word[14:12]==4);
  endfunction

  RV2WideCore dut(
    .prefetch_out(prefetch), .split_guest_access(),
    .translation_state(), .translation_flush(translation_flush), .instruction_invalidate_out(instruction_invalidate),
    .branch_update_out(branch_update), .predictor_restore_out(), .predictor_clear_out(predictor_clear),
    .direction_update_out(direction_update), .history_restore_out(history_restore),
    .ras_resolution_out(ras_resolution), .history_commit_out(history_commit),
    .interrupts(interrupts), .hart_id(64'd7), .time_counter(time_counter), .sleeping(sleeping),
    .clock(clock), .reset(reset), .instructions_in(instructions), .instructions_out(ready),
    .resolution_0_in(resolution[0]), .resolution_1_in(resolution[1]),
    .memory_stage_0_out(memory_stage[0]), .memory_stage_1_out(memory_stage[1]),
    .retired_0_out(retired[0]), .retired_1_out(retired[1]),
    .redirect_out(redirect), .fetch_flush_out(fetch_flush), .issued(issued), .retired_count(retired_count), .instruction_capacity(instruction_capacity),
    .memory_in({memory_in.request_ready, memory_in.fault, memory_in.response, memory_in.drained, 1'b0, reservation_valid}),
    .memory_out(memory_out), .split_in(split_in), .split_out(split_out), .pipeline_in(pipeline_in), .pipeline_out(pipeline_out), .completed_out(completed)
  );
  always #5 clock = ~clock;
  typedef struct packed { logic valid; logic [11:0] index; logic taken; } direction_update_t;
  typedef struct packed { logic valid; logic [9:0] bits; } history_restore_t;
  direction_update_t direction_update;
  history_restore_t history_restore;
  typedef struct packed { logic valid; logic [1:0] action; logic [63:0] return_address; logic [1:0] predicted_action; } ras_resolution_t;
  typedef struct packed { logic valid; logic [1:0] enable, taken; } history_commit_t;
  ras_resolution_t ras_resolution;
  history_commit_t history_commit;
  direction_t offered_direction[logic [63:0]];
  logic [9:0] corrected_history[logic [63:0]];
  int direction_updates=0, history_recoveries=0;
  function automatic direction_t direction_metadata(logic [63:0] pc, logic [31:0] word, bit taken=0);
    return '{word[6:0]==7'h63,12'((pc>>1)^'h2d5),10'((pc>>1)^'h155),taken};
  endfunction
  function automatic bit branch_encoding(logic [31:0] word);
    return word[6:0] inside {7'h63,7'h6f,7'h67};
  endfunction
  function automatic logic [1:0] ras_action(logic [31:0] word);
    bit rd_link, rs1_link;
    rd_link=word[11:7] inside {1,5}; rs1_link=word[19:15] inside {1,5};
    if(word[6:0]==7'h6f) return rd_link ? 1 : 0;
    if(word[6:0]!=7'h67) return 0;
    if(!rd_link) return rs1_link ? 2 : 0;
    return rs1_link && word[11:7]!=word[19:15] ? 3 : 1;
  endfunction
  always @(posedge clock) if(!reset) begin
    int ras_count;
    ras_count=0;
    for(int lane=0;lane<2;lane++) begin
      bit conditional, stack_event;
      conditional=retired[lane].valid && retired[lane].bits.fetched.instruction[6:0]==7'h63;
      assert((history_commit.valid && history_commit.enable[lane])==conditional)
        else $fatal(1,"committed history was delayed or lost a retirement lane");
      if(conditional && expected_branch_taken.exists(retired[lane].bits.fetched.pc))
        assert(history_commit.taken[lane]==expected_branch_taken[retired[lane].bits.fetched.pc])
          else $fatal(1,"committed history appended the wrong lane outcome");
      if(retired[lane].valid && branch_encoding(retired[lane].bits.fetched.instruction))
        training_pending.push_back(retired[lane].bits.fetched.pc);
      stack_event=retired[lane].valid && branch_encoding(retired[lane].bits.fetched.instruction) &&
        (ras_action(retired[lane].bits.fetched.instruction)!=0 || retired[lane].bits.fetched.speculated_ras_action!=0);
      if(stack_event) begin
        ras_count++;
        assert(ras_resolution.valid && ras_resolution.action==ras_action(retired[lane].bits.fetched.instruction) &&
          ras_resolution.return_address==retired[lane].bits.fetched.sequential_pc &&
          ras_resolution.predicted_action==retired[lane].bits.fetched.speculated_ras_action)
          else $fatal(1,"RAS resolution was delayed or selected the other branch");
      end
    end
    assert(ras_count<=1 && ras_resolution.valid==(ras_count!=0)) else $fatal(1,"multiple or spurious RAS actions");
    ras_resolutions+=ras_count;
    if(retired[0].valid && retired[1].valid && branch_encoding(retired[0].bits.fetched.instruction) && branch_encoding(retired[1].bits.fetched.instruction)) dual_branches++;
    if(predictor_clear) training_pending.delete();
    if(training_pending.size()>max_training_pending) max_training_pending=training_pending.size();
    if(branch_update.valid) begin
      logic [63:0] expected_pc;
      assert(training_pending.size()>0) else $fatal(1,"predictor trained an unretired branch");
      expected_pc=training_pending.pop_front();
      assert(branch_update.bits.pc==expected_pc) else $fatal(1,"branch training reordered: got=%h expected=%h",branch_update.bits.pc,expected_pc);
      branch_updates++;
      if(expected_branch_taken.exists(branch_update.bits.pc)) begin
        assert(branch_update.bits.taken==expected_branch_taken[branch_update.bits.pc])
          else $fatal(1,"paired branch compared stale operands pc=%h",branch_update.bits.pc);
        expected_branch_taken.delete(branch_update.bits.pc);
      end
      assert(branch_update.bits.branch) else $fatal(1,"nonbranch training payload");
      assert(direction_update.valid==branch_update.bits.conditional) else $fatal(1,"nonconditional trained BHT or conditional lost metadata");
      if(direction_update.valid) begin
        direction_updates++;
        assert(direction_update.index==offered_direction[branch_update.bits.pc].index && direction_update.taken==branch_update.bits.taken)
          else $fatal(1,"BHT training did not use saved lookup index/outcome");
        if(corrected_history.exists(branch_update.bits.pc)) begin
          assert(corrected_history[branch_update.bits.pc]=={offered_direction[branch_update.bits.pc].history[8:0],branch_update.bits.taken})
            else $fatal(1,"MEM recovery omitted the actual conditional outcome");
          corrected_history.delete(branch_update.bits.pc);
        end
      end
    end
    assert(training_pending.size()<=2) else $fatal(1,"training queue exceeded two entries");
  end
  always @(posedge clock) if(!reset && redirect.valid && redirect.bits.resolution.disposition inside {0,2}) begin
    direction_t checkpoint;
    logic [9:0] want;
    checkpoint=offered_direction[redirect.bits.pc];
    want=checkpoint.history;
    assert(history_restore.valid) else $fatal(1,"recovery omitted history checkpoint");
    if(redirect.bits.resolution.disposition==0 && checkpoint.valid)
      corrected_history[redirect.bits.pc]=history_restore.bits;
    else assert(history_restore.bits==want) else $fatal(1,"recovery history mismatch pc=%h got=%h want=%h",redirect.bits.pc,history_restore.bits,want);
    history_recoveries++;
  end
  always @(posedge clock) if(!reset && translation_flush) translation_invalidations++;
  always @(posedge clock) if(!reset && instruction_invalidate) begin
    invalidations++;
    assert(retired[0].valid && retired[0].bits.fetched.instruction==32'h0000100f && redirect.valid && redirect.bits.resolution.disposition==3)
      else $fatal(1,"invalidation must be a retiring WB FENCE.I event");
  end
  assign memory_in.ordered_busy = 1'b0;
  always_comb begin
    split_in.request_ready = !split_active;
    split_in.response.bits = split_reply;
    memory_in.fault.valid = memory_out.request.valid && inject_memory_fault && memory_out.request.bits.address == fault_address;
    memory_in.fault.bits = '{guest:'0,disposition: 2'd1, cause: memory_out.request.bits.access inside {2,4,5,6,7,8,9} ? 64'd7 : 64'd5, value: fault_address};
    memory_in.request_ready = !block_requests && response_count < 16 && !memory_in.fault.valid;
    memory_in.drained = response_count == 0 && !memory_out.request.valid;
    pipeline_in.response = lookup_response;
    pipeline_in.commit_ready = store_candidate_valid && !block_stores;
    for (int lane = 0; lane < 2; lane++) begin
      resolution[lane] = '0;
      if (inject_enable && memory_stage[lane].valid && memory_stage[lane].bits.pc == inject_pc) begin
        resolution[lane].valid = 1;
        resolution[lane].bits = inject_result;
      end
    end
  end
  always @(negedge clock) split_in.response.valid = !reset && split_active && cycles >= split_due && !hold_split;

  always @(negedge clock) memory_in.response.valid = !reset && response_count > 0 && cycles >= response_due[response_read] && !hold_responses;
  assign memory_in.response.bits = response_data[response_read];

  always @(posedge clock) begin
    if (reset) begin
      split_active <= 0;
      response_count <= 0; response_read <= 0; response_write <= 0;
      lookup_response <= '0; store_candidate_valid <= 0;
    end else begin
      logic push_response, pop_response;
      if(split_out.valid && split_in.request_ready) begin
        logic [63:0] data;
        int bytes_count;
        assert(response_count==0 && expected_completions.size()==0) else $fatal(1,"split issued before older completions drained");
        assert(split_out.bits.access inside {1,2}) else $fatal(1,"split atomic");
        assert(split_out.bits.locality==expected_split_locality) else $fatal(1,"split lost locality");
        bytes_count=1<<split_out.bits.width; data=0;
        for(int b=0;b<bytes_count;b++) begin
          if(split_out.bits.access==2 && !split_fault) memory_bytes[int'(split_out.bits.address)+b]=split_out.bits.data[b*8+:8];
          data[b*8+:8]=memory_bytes[int'(split_out.bits.address)+b];
        end
        if(!split_out.bits.unsigned_load && bytes_count<8 && data[bytes_count*8-1]) data|='1<<(bytes_count*8);
        split_reply <= '{response:'{access_fault:1'b0,data:data,context_bit:1'b0},page_fault:split_fault,fault_address:split_out.bits.address+3,guest:'0};
        split_active <= 1; split_due <= cycles+12; split_requests++;
        if($test$plusargs("debug")) $display("%0d split request %h width=%0d",cycles,split_out.bits.address,split_out.bits.width);
      end else if(split_in.response.valid) split_active<=0;
      push_response = memory_out.request.valid && memory_in.request_ready;
      pop_response = memory_in.response.valid && memory_out.response_ready;
      response_count <= response_count + int'(push_response) - int'(pop_response);
      if (response_count > max_outstanding) max_outstanding = response_count;
      lookup_response.valid <= pipeline_out.request.valid;
      if (pipeline_out.request.valid) lookups++;
      lookup_request <= pipeline_out.request.bits;
      lookup_response.bits.outcome <= lookup_mode == 1 ? (pipeline_out.request.bits.access == 2 ? 3'd2 : 3'd1) : 3'(lookup_mode);
      for (int b = 0; b < 8; b++)
        lookup_response.bits.data[b*8 +: 8] <= memory_bytes[int'(pipeline_out.request.bits.address[11:3])*8 + b];
      store_candidate <= lookup_request;
      store_candidate_valid <= lookup_response.valid && lookup_response.bits.outcome == 2;
      if (memory_out.request.valid && !memory_in.request_ready) begin
        stall_cycles++;
      end
      if (push_response) begin
        logic [63:0] beat;
        check_request(memory_out.request.bits);
        for (int b = 0; b < 8; b++)
          beat[b*8 +: 8] = memory_bytes[int'(memory_out.request.bits.address[11:3])*8 + b];
        if(memory_out.request.bits.access==3 && memory_out.request.bits.width==2) begin
          beat=beat>>(8*int'(memory_out.request.bits.address[2:0]));
          beat={{32{beat[31]}},beat[31:0]};
        end
        response_data[response_write] <= beat;
        response_management[response_write] <= memory_out.request.bits.access inside {7,8,9};
        response_due[response_write] <= cycles + configured_delay;
        response_write <= (response_write + 1) % 16;
        requests++;
      end
      if (pipeline_out.commit && pipeline_in.commit_ready) begin
        assert (store_candidate_valid) else $fatal(1, "store commit without live candidate");
        check_request(store_candidate);
        hits++;
      end
      if (pop_response) begin
        response_read <= (response_read + 1) % 16;
        responses++;
      end
    end
  end

  always @(posedge clock) begin
    if (!reset) begin
      cycles++;
      begin
        bit p0, p1;
        logic [63:0] pc;
        p0=retired[0].valid && prefetch_encoding(retired[0].bits.fetched.instruction);
        p1=retired[1].valid && prefetch_encoding(retired[1].bits.fetched.instruction);
        assert(prefetch.valid==(p0||p1)) else $fatal(1,"prefetch was not the successful WB prefix");
        if(p0||p1) begin
          pc=p0 ? retired[0].bits.fetched.pc : retired[1].bits.fetched.pc;
          assert(prefetch.bits==expected_prefetch[pc]) else $fatal(1,"prefetch address/intent pc=%h",pc);
          assert(!(p0 && retired[0].bits.deferred) && !(p1 && retired[1].bits.deferred)) else $fatal(1,"prefetch allocated deferred work");
          prefetch_count++;
          if(p0&&p1) prefetch_pairs++;
        end
      end
      if($test$plusargs("debug") && split_in.response.valid) $display("%0d split response retire=%b%b redirect=%b data=%h",cycles,retired[1].valid,retired[0].valid,redirect.valid,split_in.response.bits.response.data);
      for(int lane=0;lane<2;lane++) if(memory_stage[lane].valid) begin
        if(memory_stage[lane].bits.pc=='hab08) multiply_mem_cycle=cycles;
        if(memory_stage[lane].bits.pc=='hab10) dependent_mem_cycle=cycles;
      end
      if(memory_stage[0].valid && memory_stage[1].valid && memory_stage[0].bits.instruction[6:0]==7'h17 &&
         memory_stage[0].bits.instruction[11:7]!=0 && memory_stage[0].bits.instruction[11:7]==memory_stage[1].bits.instruction[19:15] &&
         memory_stage[1].bits.instruction[6:0]==7'h13 && memory_stage[1].bits.instruction[14:12]==0)
        auipc_addi_pairs++;
      if(memory_stage[0].valid && memory_stage[1].valid && memory_stage[0].bits.instruction[11:7]!=0 &&
         memory_stage[0].bits.instruction[11:7]==memory_stage[1].bits.instruction[19:15] &&
         (memory_stage[0].bits.instruction[6:0] inside {7'h13,7'h1b,7'h33,7'h3b,7'h37,7'h17,7'h03,7'h2f,7'h6f,7'h67} || mop_encoding(memory_stage[0].bits.instruction)) &&
         memory_stage[1].bits.instruction[6:0]==7'h03) begin
        assert(memory_stage[1].bits.instruction[31:20]==0 &&
               !(memory_stage[0].bits.instruction[6:0] inside {7'h03,7'h2f,7'h6f,7'h67}) &&
               !((memory_stage[0].bits.instruction[6:0] inside {7'h33,7'h3b}) && memory_stage[0].bits.instruction[31:25]==1))
          else $fatal(1,"dependent load crossed forbidden pairing boundary");
        address_pairs++;
      end
      if(memory_stage[0].valid && memory_stage[1].valid && memory_stage[1].bits.instruction==32'h6c00c1f3 &&
         memory_stage[0].bits.instruction==imm(1,0,'h300)) guest_address_pairs++;
      if(memory_stage[0].valid && memory_stage[1].valid && memory_stage[0].bits.instruction[11:7]!=0 &&
         memory_stage[0].bits.instruction[11:7]==memory_stage[1].bits.instruction[24:20] &&
         (memory_stage[0].bits.instruction[6:0] inside {7'h13,7'h1b,7'h33,7'h3b,7'h37,7'h17,7'h03,7'h6f,7'h67} || mop_encoding(memory_stage[0].bits.instruction)) &&
         memory_stage[1].bits.instruction[6:0]==7'h23) begin
        assert(memory_stage[0].bits.instruction[11:7]!=memory_stage[1].bits.instruction[19:15] &&
               !(memory_stage[0].bits.instruction[6:0] inside {7'h03,7'h6f,7'h67}) &&
               !((memory_stage[0].bits.instruction[6:0] inside {7'h33,7'h3b}) && memory_stage[0].bits.instruction[31:25]==1))
          else $fatal(1,"dependent store crossed forbidden pairing boundary");
        store_data_pairs++;
      end
      if(memory_stage[0].valid && memory_stage[1].valid && memory_stage[0].bits.instruction[11:7]!=0 &&
         (memory_stage[0].bits.instruction[6:0] inside {7'h13,7'h1b,7'h33,7'h3b,7'h37,7'h17,7'h03,7'h6f,7'h67} || mop_encoding(memory_stage[0].bits.instruction)) &&
         memory_stage[1].bits.instruction[6:0]==7'h63 &&
         (memory_stage[0].bits.instruction[11:7]==memory_stage[1].bits.instruction[19:15] ||
          memory_stage[0].bits.instruction[11:7]==memory_stage[1].bits.instruction[24:20])) begin
        assert(!(memory_stage[0].bits.instruction[6:0] inside {7'h03,7'h6f,7'h67}) &&
               !((memory_stage[0].bits.instruction[6:0] inside {7'h33,7'h3b}) && memory_stage[0].bits.instruction[31:25]==1))
          else $fatal(1,"deferred/control-transfer producer paired with dependent branch");
        branch_pairs++;
      end
      if(memory_stage[0].valid && memory_stage[1].valid && memory_stage[0].bits.instruction[11:7]!=0 &&
         memory_stage[0].bits.instruction[6:0] inside {7'h13,7'h1b,7'h33,7'h3b,7'h37,7'h17} &&
         memory_stage[1].bits.instruction[6:0]==7'h67)
        assert(memory_stage[0].bits.instruction[11:7]!=memory_stage[1].bits.instruction[19:15])
          else $fatal(1,"dependent JALR target paired without an EX base");
      if (cycles > 80000) $fatal(1, "watchdog: commits=%0d pending=%0d next_pc=%h conditional_pairs=%0d mop_pairs=%0d split=%0d/%b offer=%b response=%b loads=%0d requests=%0d completions=%0d",commits,expected.size(),expected.size()!=0 ? expected[0].fetched.pc : 0,conditional_dual,mop_dual,split_requests,split_active,split_out.valid,split_in.response.valid,response_count,expected_requests.size(),expected_completions.size());
      assert (issued <= 2 && retired_count <= 2) else $fatal(1, "non-prefix count");
      if (int'(instruction_capacity) < minimum_instruction_capacity) minimum_instruction_capacity = int'(instruction_capacity);
      assert (!retired[1].valid || retired[0].valid) else $fatal(1, "younger retired alone");
      if(memory_stage[0].valid && serializing_encoding(memory_stage[0].bits.instruction))
        assert(!memory_stage[1].valid) else $fatal(1,"system instruction did not issue alone");
      if(memory_stage[1].valid)
        assert(!serializing_encoding(memory_stage[1].bits.instruction)) else $fatal(1,"system instruction in younger slot");
      if (retired[0].valid && retired[1].valid && retired[0].bits.write && retired[1].bits.write && retired[0].bits.rd == retired[1].bits.rd) begin
        assert (!retired[0].bits.deferred) else $fatal(1, "deferred older writer paired with same destination");
        if (retired[1].bits.deferred) waw_deferred++;
        else waw_dual++;
      end
      assert (int'(retired_count) == int'(retired[0].valid) + int'(retired[1].valid)) else $fatal(1, "retirement count mismatch");
      if (issued == 1) single_issues++;
      if (response_count > 0 && retired_count != 0) overlap_retirements++;
      if (completed.valid && completed.bits.write) begin
        assert (!retired[1].valid) else $fatal(1, "completion collided with younger slot");
        if (retired[0].valid && retired[0].bits.write && !retired[0].bits.deferred) shared_writes++;
      end
      if (retired_count == 2) begin
        if(conditional_encoding(retired[0].bits.fetched.instruction) && conditional_encoding(retired[1].bits.fetched.instruction)) conditional_dual++;
        if(mop_encoding(retired[0].bits.fetched.instruction) && mop_encoding(retired[1].bits.fetched.instruction)) mop_dual++;
        dual_commits++;
        dual_run++;
        if (dual_run > longest_dual_run) longest_dual_run = dual_run;
      end else dual_run = 0;
      if (retired[0].valid && retired[1].valid && retired[0].bits.fetched.pc == 'h204 && retired[1].bits.fetched.pc == 'h208)
        saw_repacked = 1;
      for (int lane = 0; lane < 2; lane++) begin
        if (retired[lane].valid) begin
          retirement_t want;
          assert (expected.size() > 0) else $fatal(1, "unexpected retirement pc=%h", retired[lane].bits.fetched.pc);
          want = expected.pop_front();
          if(want.fetched.instruction==32'h0100000f) begin
            assert(!retired[lane].bits.write && !retired[lane].bits.deferred && !sleeping)
              else $fatal(1,"PAUSE allocated architectural work or entered sleep");
            pause_commits++; pause_cycle=cycles;
            if(lane==1) pause_dual++;
          end
          if(want.fetched.instruction[6:0]==7'h0f && want.fetched.instruction[14:12]==2 && want.fetched.instruction[31:20]<3) begin
            assert(!retired[lane].bits.deferred && memory_in.response.valid && memory_out.response_ready && response_management[response_read])
              else $fatal(1,"maintenance retired before its response or allocated deferred RF work");
          end
          assert (retired[lane].bits.fetched == want.fetched && retired[lane].bits.write == want.write)
            else $fatal(1, "retirement order/control pc=%h expected=%h", retired[lane].bits.fetched.pc, want.fetched.pc);
          if (retired[lane].bits.deferred) begin
            expected_completions.push_back(want);
            if ((want.fetched.instruction[6:0] inside {7'h33,7'h3b}) && want.fetched.instruction[31:25]==1 && want.fetched.instruction[14:12]<4)
              multiply_authorized_cycle[want.fetched.pc]=cycles;
            if(want.fetched.instruction[6:0] inside {7'h03,7'h23,7'h2f,7'h0f} || (want.fetched.instruction[31:28]==4'h6 && want.fetched.instruction[14:12]==4)) response_owners.push_back(want);
          end
          if (want.write && !retired[lane].bits.deferred)
            assert (retired[lane].bits.rd == want.rd && retired[lane].bits.data == want.data)
              else $fatal(1, "result pc=%h rd=%d got=%h expected rd=%d data=%h", want.fetched.pc, retired[lane].bits.rd, retired[lane].bits.data, want.rd, want.data);
          commits++;
        end
      end
      if (memory_in.response.valid && memory_out.response_ready && !response_management[response_read]) begin
        retirement_t owner;
        assert (response_owners.size() > 0) else $fatal(1, "response has no accepted owner");
        owner = response_owners.pop_front();
        if (owner.write) begin
          assert (issued <= 1) else $fatal(1, "load response did not reserve younger issue slot");
          reserved_slots++;
        end
      end
      if (completed.valid) begin
        retirement_t want;
        int index;
        if (completed.bits.fetched.pc>='hac08 && completed.bits.fetched.pc<'hac48) begin
          if (multiply_stream_count!=0) assert(cycles==multiply_stream_cycle+1)
            else $fatal(1,"independent multiplies did not write on consecutive cycles");
          multiply_stream_cycle=cycles; multiply_stream_count++;
        end
        if (multiply_authorized_cycle.exists(completed.bits.fetched.pc)) begin
          assert(cycles==multiply_authorized_cycle[completed.bits.fetched.pc]+1)
            else $fatal(1,"multiply missed fixed EX+3 writeback pc=%h",completed.bits.fetched.pc);
          multiply_authorized_cycle.delete(completed.bits.fetched.pc);
        end
        index=-1;
        assert (expected_completions.size() > 0) else $fatal(1, "unowned memory completion");
        foreach(expected_completions[i]) if(expected_completions[i].fetched.pc==completed.bits.fetched.pc) index=i;
        assert(index>=0) else $fatal(1,"completion has no accepted owner pc=%h",completed.bits.fetched.pc);
        want = expected_completions[index]; expected_completions.delete(index);
        assert (completed.bits.fetched == want.fetched && completed.bits.write == want.write && !completed.bits.deferred) else $fatal(1, "completion owner mismatch");
        if (want.write) assert (completed.bits.rd == want.rd && completed.bits.data == want.data) else $fatal(1, "load completion mismatch pc=%h got=%h want=%h", want.fetched.pc, completed.bits.data, want.data);
      end
      if (redirect.valid) begin
        redirect_t want;
        if (redirect.bits.resolution.disposition == 0) begin
          assert ((memory_stage[0].valid && memory_stage[0].bits.pc == redirect.bits.pc) ||
                  (memory_stage[1].valid && memory_stage[1].bits.pc == redirect.bits.pc))
            else $fatal(1, "branch recovery did not occur in MEM");
          for (int lane = 0; lane < 2; lane++)
            assert (!retired[lane].valid || retired[lane].bits.fetched.pc != redirect.bits.pc)
              else $fatal(1, "MEM branch was reported as already retired");
          mem_branch_redirects++;
        end else begin
          for (int lane = 0; lane < 2; lane++)
            if (memory_stage[lane].valid && memory_stage[lane].bits.instruction[6:0] == 7'h6f) wb_overrides++;
        end
        assert (expected_redirects.size() > 0) else $fatal(1, "unexpected redirect pc=%h", redirect.bits.pc);
        want = expected_redirects.pop_front();
        assert (redirect.bits.pc == want.pc && redirect.bits.target == want.target && redirect.bits.resolution.disposition == want.resolution.disposition)
          else $fatal(1, "redirect mismatch got=%h want=%h", redirect.bits, want);
        if (want.resolution.disposition == 1)
          assert (redirect.bits.resolution.cause == want.resolution.cause && redirect.bits.resolution.value == want.resolution.value && response_count == 0 && expected_completions.size() == 0)
            else $fatal(1, "fault provenance mismatch");
        assert (!ready && issued == 0) else $fatal(1, "accepted younger work on redirect");
        stops++;
      end
    end
  end

  function automatic logic [31:0] imm(int rd, int rs1, int value, int f3 = 0, int op = 'h13);
    return {12'(value), 5'(rs1), 3'(f3), 5'(rd), 7'(op)};
  endfunction
  function automatic logic [31:0] regop(int rd, int rs1, int rs2, int f3 = 0, int f7 = 0, int op = 'h33);
    return {7'(f7), 5'(rs2), 5'(rs1), 3'(f3), 5'(rd), 7'(op)};
  endfunction
  function automatic logic [31:0] branch(int rs1, int rs2, int offset, int f3 = 0);
    logic [12:0] b;
    b = 13'(offset);
    return {b[12], b[10:5], 5'(rs2), 5'(rs1), 3'(f3), b[4:1], b[11], 7'h63};
  endfunction
  function automatic logic [31:0] jump(int rd, int offset);
    logic [20:0] j;
    j = 21'(offset);
    return {j[20], j[10:1], j[11], j[19:12], 5'(rd), 7'h6f};
  endfunction
  function automatic logic [31:0] store(int rs2, int rs1, int offset, int width);
    logic [11:0] s;
    s = 12'(offset);
    return {s[11:5], 5'(rs2), 5'(rs1), 3'(width), s[4:0], 7'h23};
  endfunction

  task automatic expect_memory(logic [63:0] address, bit write_access, int bytes, logic [63:0] value);
    memory_req_t item;
    item = '0;
    item.address = address;
    item.access = write_access ? 2 : 1;
    item.width = 2'($clog2(bytes));
    for (int b = 0; b < bytes; b++) begin
      item.mask[int'(address[2:0]) + b] = 1;
      item.data[(int'(address[2:0]) + b)*8 +: 8] = value[b*8 +: 8];
    end
    expected_requests.push_back(item);
  endtask

  task automatic check_request(memory_req_t actual);
    memory_req_t want;
    assert (expected_requests.size() > 0) else $fatal(1, "unowned or duplicate memory effect address=%h", actual.address);
    want = expected_requests.pop_front();
    assert (actual.address == want.address && actual.access == want.access && actual.width == want.width && actual.locality == want.locality && (want.access>=6 || actual.mask == want.mask)) else $fatal(1, "request mismatch got=%h expected=%h", actual, want);
    if(want.access==6) for(int b=0;b<64;b++) memory_bytes[int'(actual.address&~64'd63)+b]<=0;
    if (want.access == 2) begin
      for (int b = 0; b < 8; b++) begin
        if (want.mask[b]) begin
          assert (actual.data[b*8 +: 8] == want.data[b*8 +: 8]) else $fatal(1, "store data mismatch");
          memory_bytes[int'(actual.address[11:3])*8+b] <= actual.data[b*8 +: 8];
        end
      end
      stores++;
    end
  endtask

  // Independent architectural B oracle: bit loops rather than the RTL's shared
  // shifter/count/adder implementation. Results include each instruction's own
  // word projection; ADD.UW, SLLI.UW, and ZEXT.H are not sign-extended word ops.
  function automatic bit bitmanip_value(logic [31:0] word, logic [63:0] a,b, output logic [63:0] value);
    int op=int'(word[6:0]), f3=int'(word[14:12]), f7=int'(word[31:25]);
    int n=(op=='h3b || op=='h1b) ? 32 : 64;
    int shift=int'(b[5:0]);
    bit right;
    value=0;
    if(op=='h13 || op=='h1b) begin
      if(f3==1 && word[31:20]>='h600 && word[31:20]<='h602) begin
        case(word[21:20])
          0: begin value=64'(n); for(int i=0;i<n;i++) if(a[i]) value=64'(n)-64'd1-64'(i); end
          1: begin value=64'(n); for(int i=n-1;i>=0;i--) if(a[i]) value=64'(i); end
          2: for(int i=0;i<n;i++) value+=64'(a[i]);
        endcase
        return 1;
      end
      if(op=='h13 && f3==1 && word[31:20]=='h604) begin value={{56{a[7]}},a[7:0]}; return 1; end
      if(op=='h13 && f3==1 && word[31:20]=='h605) begin value={{48{a[15]}},a[15:0]}; return 1; end
      if(op=='h13 && f3==5 && word[31:20]=='h287) begin
        for(int i=0;i<8;i++) value[i*8+:8]=a[i*8+:8]!=0 ? 8'hff : 0;
        return 1;
      end
      if(op=='h13 && f3==5 && word[31:20]=='h6b8) begin
        for(int i=0;i<8;i++) value[i*8+:8]=a[(7-i)*8+:8];
        return 1;
      end
      shift=int'(word[25:20]);
      if(op=='h1b && f3==1 && word[31:26]==2) begin value={32'd0,a[31:0]}<<shift; return 1; end
      if((op=='h13 && word[31:26]=='h18 && f3==5) || (op=='h1b && f7=='h30 && f3==5)) right=1;
      else if(op=='h13 && word[31:26]=='h12 && f3==1) begin value=a&~(64'd1<<shift); return 1; end
      else if(op=='h13 && word[31:26]=='h12 && f3==5) begin value=64'(a[shift]); return 1; end
      else if(op=='h13 && word[31:26]=='h1a && f3==1) begin value=a^(64'd1<<shift); return 1; end
      else if(op=='h13 && word[31:26]=='h0a && f3==1) begin value=a|(64'd1<<shift); return 1; end
      else return 0;
    end else if(op=='h33 || op=='h3b) begin
      if(f7=='h10 && (f3==2 || f3==4 || f3==6)) begin
        value=((op=='h3b ? {32'd0,a[31:0]} : a)<<(f3/2))+b; return 1;
      end
      if(op=='h3b && f7==4 && f3==0) begin value={32'd0,a[31:0]}+b; return 1; end
      if(op=='h3b && f7==4 && f3==4 && word[24:20]==0) begin value={48'd0,a[15:0]}; return 1; end
      if(op=='h33 && f7=='h20) case(f3)
        4: begin value=~(a^b); return 1; end
        6: begin value=a|~b; return 1; end
        7: begin value=a&~b; return 1; end
        default: return 0;
      endcase
      if(op=='h33 && f7==5) case(f3)
        4: begin value=$signed(a)<$signed(b) ? a : b; return 1; end
        5: begin value=a<b ? a : b; return 1; end
        6: begin value=$signed(a)>$signed(b) ? a : b; return 1; end
        7: begin value=a>b ? a : b; return 1; end
        default: return 0;
      endcase
      if(f7=='h30 && (f3==1 || f3==5)) right=f3==5;
      else if(op=='h33 && f7=='h24 && f3==1) begin value=a&~(64'd1<<shift); return 1; end
      else if(op=='h33 && f7=='h24 && f3==5) begin value=64'(a[shift]); return 1; end
      else if(op=='h33 && f7=='h34 && f3==1) begin value=a^(64'd1<<shift); return 1; end
      else if(op=='h33 && f7=='h14 && f3==1) begin value=a|(64'd1<<shift); return 1; end
      else return 0;
    end else return 0;
    shift%=n;
    for(int i=0;i<n;i++) value[i]=a[right ? (i+shift)%n : (i+n-shift)%n];
    if(n==32) value={{32{value[31]}},value[31:0]};
    return 1;
  endfunction

  task automatic expect_instruction(logic [63:0] pc, logic [31:0] word);
    retirement_t item;
    logic [63:0] a, b, value, immediate;
    logic signed [31:0] narrow;
    int op, f3, f7;
    bit writes;
    a = model[word[19:15]];
    b = model[word[24:20]];
    immediate = {{52{word[31]}}, word[31:20]};
    op = int'(word[6:0]); f3 = int'(word[14:12]); f7 = int'(word[31:25]);
    writes = 1;
    value = 0;
    if(prefetch_encoding(word)) expected_prefetch[pc]='{address:a+64'($signed({word[31:25],5'b0})),operation:word[24:20]==3 ? 2'd3 : 2'(word[24:20]+1)};
    if(!bitmanip_value(word,a,b,value)) begin
    case (op)
      'h0f: begin
        memory_req_t request;
        if(word==32'h0100000f) writes=0;
        else begin
        assert(word[31:20] inside {0,1,2,4} && f3==2 && word[11:7]==0) else $fatal(1,"oracle unsupported CBO");
        request='0; request.address=a; request.access=word[31:20]==4 ? 6 : 4'(7+word[31:20]); request.width=3;
        expected_requests.push_back(request); writes=0;
        if(word[31:20]==4) for(int i=0;i<64;i++) model_bytes[int'(a&~64'd63)+i]=0;
        end
      end
      'h73: begin
        assert(mop_encoding(word)) else $fatal(1,"oracle unsupported SYSTEM encoding %h",word);
        value=0;
      end
      'h37: value = {{32{word[31]}}, word[31:12], 12'b0};
      'h17: value = pc + {{32{word[31]}}, word[31:12], 12'b0};
      'h6f, 'h67: value = pc + 4;
      'h63: writes = 0;
      'h03, 'h23: begin
        logic [63:0] address;
        int bytes;
        if (op == 'h23) immediate = {{52{word[31]}}, word[31:25], word[11:7]};
        address = a + immediate;
        bytes = 1 << (f3 & 3);
        assert (address+64'(bytes) <= 4096) else $fatal(1, "oracle expected outside access");
        if (address % 64'(bytes) == 0 && (lookup_mode != 1 || op == 'h23)) expect_memory(address, op == 'h23, bytes, b);
        if (op == 'h23) begin
          writes = 0;
          for (int i = 0; i < bytes; i++) model_bytes[int'(address) + i] = b[i*8 +: 8];
        end else begin
          for (int i = 0; i < bytes; i++) value[i*8 +: 8] = model_bytes[int'(address) + i];
          if ((f3 & 4) == 0 && bytes < 8 && value[bytes*8-1]) value |= '1 << (bytes*8);
        end
      end
      'h2f: begin
        memory_req_t request;
        int bytes=1<<f3;
        assert(word[31:27]==2) else $fatal(1,"unmodeled accepted core atomic");
        request='0; request.address=a; request.access=3; request.width=2'(f3);
        for(int i=0;i<bytes;i++) begin
          request.mask[int'(a[2:0])+i]=1;
          value[i*8+:8]=model_bytes[int'(a)+i];
        end
        if(bytes==4) value={{32{value[31]}},value[31:0]};
        expected_requests.push_back(request);
      end
      'h13, 'h1b: begin
        case (f3)
          0: value = a + immediate;
          1: value = a << (op == 'h1b ? int'(word[24:20]) : int'(word[25:20]));
          2: value = 64'($signed(a) < $signed(immediate));
          3: value = 64'(a < immediate);
          4: value = a ^ immediate;
          5: begin
            if (op == 'h1b) begin
              narrow = a[31:0];
              value = word[30] ? 64'(narrow >>> word[24:20]) : 64'(a[31:0] >> word[24:20]);
            end else value = word[30] ? $unsigned($signed(a) >>> word[25:20]) : a >> word[25:20];
          end
          6: value = a | immediate;
          7: value = a & immediate;
        endcase
      end
      'h33, 'h3b: begin
        if(conditional_encoding(word)) value=((f3==5 && b==0) || (f3==7 && b!=0)) ? 0 : a;
        else if(f7==1) begin
          logic signed [127:0] left_wide, right_wide, product;
          if(op=='h3b) begin
            a=(f3==5 || f3==7) ? {32'd0,a[31:0]} : {{32{a[31]}},a[31:0]};
            b=(f3==5 || f3==7) ? {32'd0,b[31:0]} : {{32{b[31]}},b[31:0]};
          end
          left_wide=f3==3 ? $signed({64'd0,a}) : $signed({{64{a[63]}},a});
          right_wide=(f3==2 || f3==3) ? $signed({64'd0,b}) : $signed({{64{b[63]}},b});
          product=left_wide*right_wide;
          case(f3)
            0: value=product[63:0];
            1,2,3: value=product[127:64];
            4: value=b==0 ? '1 : (a==64'h8000000000000000 && b=='1 ? a : $unsigned($signed(a)/$signed(b)));
            5: value=b==0 ? '1 : a/b;
            6: value=b==0 ? a : (a==64'h8000000000000000 && b=='1 ? 0 : $unsigned($signed(a)%$signed(b)));
            7: value=b==0 ? a : a%b;
          endcase
        end else case (f3)
          0: value = f7 == 32 ? a - b : a + b;
          1: value = a << (op == 'h3b ? int'(b[4:0]) : int'(b[5:0]));
          2: value = 64'($signed(a) < $signed(b));
          3: value = 64'(a < b);
          4: value = a ^ b;
          5: begin
            if (op == 'h3b) begin
              narrow = a[31:0];
              value = f7 == 32 ? 64'(narrow >>> b[4:0]) : 64'(a[31:0] >> b[4:0]);
            end else value = f7 == 32 ? $unsigned($signed(a) >>> b[5:0]) : a >> b[5:0];
          end
          6: value = a | b;
          7: value = a & b;
        endcase
      end
      default: $fatal(1, "oracle unsupported instruction %h", word);
    endcase
    if (op == 'h1b || op == 'h3b) value = {{32{value[31]}}, value[31:0]};
    end
    item = '0;
    item.fetched.pc = pc; item.fetched.instruction = word; item.fetched.raw_instruction = word; item.fetched.sequential_pc = pc + 4;
    item.fetched.direction=direction_metadata(pc,word);
    item.rd = word[11:7]; item.write = writes && word[11:7] != 0; item.data = value;
    if (item.write) model[item.rd] = value;
    expected.push_back(item);
  endtask

  function automatic logic [31:0] m_insn(int rd, rs1, rs2, funct3, bit word=0);
    return {7'd1,5'(rs2),5'(rs1),3'(funct3),5'(rd),word ? 7'h3b : 7'h33};
  endfunction
  function automatic logic [31:0] atomic_insn(int operation, width, rd, rs1, rs2=0);
    return {5'(operation),2'b11,5'(rs2),5'(rs1),3'(width),5'(rd),7'h2f};
  endfunction
  function automatic logic [31:0] mop_insn(bit two_sources, int index, rd, rs1, rs2=0);
    logic [31:0] word;
    if(two_sources) word=32'h82004073|(32'(index&4)<<28)|(32'(index&2)<<26)|(32'(index&1)<<26)|(32'(rs2)<<20);
    else word=32'h81c04073|(32'(index&16)<<26)|(32'(index&8)<<24)|(32'(index&4)<<24)|(32'(index&3)<<20);
    return word|(32'(rs1)<<15)|(32'(rd)<<7);
  endfunction
  function automatic logic [31:0] b_insn(int operation, rd, rs1, rs2, shift=0);
    logic [31:0] word;
    bit binary_source=1;
    case(operation)
      0: word=32'h20002033; // SH1ADD
      1: word=32'h20004033; // SH2ADD
      2: word=32'h20006033; // SH3ADD
      3: word=32'h0800003b; // ADD.UW
      4: word=32'h2000203b; // SH1ADD.UW
      5: word=32'h2000403b; // SH2ADD.UW
      6: word=32'h2000603b; // SH3ADD.UW
      7: begin word=32'h0800101b|(32'(shift&63)<<20); binary_source=0; end // SLLI.UW
      8: word=32'h40007033; // ANDN
      9: word=32'h40006033; // ORN
      10: word=32'h40004033; // XNOR
      11: begin word=32'h60001013; binary_source=0; end // CLZ
      12: begin word=32'h6000101b; binary_source=0; end // CLZW
      13: begin word=32'h60101013; binary_source=0; end // CTZ
      14: begin word=32'h6010101b; binary_source=0; end // CTZW
      15: begin word=32'h60201013; binary_source=0; end // CPOP
      16: begin word=32'h6020101b; binary_source=0; end // CPOPW
      17: word=32'h0a004033; // MIN
      18: word=32'h0a005033; // MINU
      19: word=32'h0a006033; // MAX
      20: word=32'h0a007033; // MAXU
      21: begin word=32'h28705013; binary_source=0; end // ORC.B
      22: begin word=32'h6b805013; binary_source=0; end // REV8
      23: word=32'h60001033; // ROL
      24: word=32'h6000103b; // ROLW
      25: word=32'h60005033; // ROR
      26: word=32'h6000503b; // RORW
      27: begin word=32'h60005013|(32'(shift&63)<<20); binary_source=0; end // RORI
      28: begin word=32'h6000501b|(32'(shift&31)<<20); binary_source=0; end // RORIW
      29: begin word=32'h60401013; binary_source=0; end // SEXT.B
      30: begin word=32'h60501013; binary_source=0; end // SEXT.H
      31: begin word=32'h0800403b; binary_source=0; end // ZEXT.H
      32: word=32'h48001033; // BCLR
      33: begin word=32'h48001013|(32'(shift&63)<<20); binary_source=0; end // BCLRI
      34: word=32'h48005033; // BEXT
      35: begin word=32'h48005013|(32'(shift&63)<<20); binary_source=0; end // BEXTI
      36: word=32'h68001033; // BINV
      37: begin word=32'h68001013|(32'(shift&63)<<20); binary_source=0; end // BINVI
      38: word=32'h28001033; // BSET
      39: begin word=32'h28001013|(32'(shift&63)<<20); binary_source=0; end // BSETI
      default: $fatal(1,"unknown authored B operation");
    endcase
    return word|(32'(rs1)<<15)|(32'(rd)<<7)|(binary_source ? 32'(rs2)<<20 : 0);
  endfunction
  // Construct arbitrary architectural operands through real instructions.
  task automatic constant64(ref logic [63:0] pc, input int rd, input logic [63:0] value);
    send(pc,imm(rd,0,0),0,1); pc+=4;
    for(int byte_index=7;byte_index>=0;byte_index--) begin
      send(pc,imm(rd,rd,8,1),imm(rd,rd,int'(value[byte_index*8+:8]))); pc+=8;
    end
  endtask

  task automatic tick;
    @(posedge clock); #1;
  endtask
  task automatic send(logic [63:0] pc, logic [31:0] first, logic [31:0] second, int count = 2, bit keep0 = 1, bit keep1 = 1, int fetch_fault_lane = -1, prediction_t second_prediction = '0, prediction_t first_prediction = '0);
    int expected_index=expected.size();
    if (keep0) expect_instruction(pc, first);
    if (count == 2 && keep1) expect_instruction(pc + 4, second);
    instructions.valid = 1;
    instructions.bits.count = 2'(count);
    instructions.bits.entries[0] = '{pc: pc, instruction: first, raw_instruction: first, sequential_pc: pc + 4, compressed_illegal: 0, fault: '0, prediction: first_prediction, default: '0};
    instructions.bits.entries[1] = '{pc: pc + 4, instruction: second, raw_instruction: second, sequential_pc: pc + 8, compressed_illegal: 0, fault: '0, prediction: second_prediction, default: '0};
    for(int lane=0;lane<count;lane++) begin
      instructions.bits.entries[lane].direction=direction_metadata(instructions.bits.entries[lane].pc,instructions.bits.entries[lane].instruction,instructions.bits.entries[lane].prediction.valid);
      instructions.bits.entries[lane].speculated_ras_action=instructions.bits.entries[lane].prediction.ras_action;
      offered_direction[instructions.bits.entries[lane].pc]=instructions.bits.entries[lane].direction;
      corrected_history.delete(instructions.bits.entries[lane].pc);
    end
    if(keep0) begin
      expected[expected_index].fetched.prediction=first_prediction;
      expected[expected_index].fetched.speculated_ras_action=first_prediction.ras_action;
      expected[expected_index].fetched.direction=instructions.bits.entries[0].direction;
    end
    if(count==2 && keep1) begin
      expected_index+=keep0 ? 1 : 0;
      expected[expected_index].fetched.prediction=second_prediction;
      expected[expected_index].fetched.speculated_ras_action=second_prediction.ras_action;
      expected[expected_index].fetched.direction=instructions.bits.entries[1].direction;
    end
    if (fetch_fault_lane >= 0)
      instructions.bits.entries[fetch_fault_lane].fault = '{valid: 1'b1, bits: '{guest:'0,cause: 64'd1, value: pc + 4*64'(fetch_fault_lane)}};
    #1;
    while (!ready) tick();
    tick();
    instructions.valid = 0;
  endtask
  task automatic drain;
    instructions.valid = 0;
    do tick(); while (expected.size() != 0 || expected_redirects.size() != 0 || expected_requests.size() != 0 || expected_completions.size() != 0 || response_count != 0 || split_active || training_pending.size()!=0);
    repeat (8) tick();
    assert (expected.size() == 0 && expected_redirects.size() == 0) else $fatal(1, "missing ordered outcomes");
  endtask
  task automatic send_predicted(logic [63:0] pc, logic [31:0] first, logic [63:0] predicted_target, successor_pc, bit keep_successor=1, logic [1:0] action=0);
    instruction_t first_token, second_token;
    int index=expected.size();
    expect_instruction(pc,first);
    expected[index].fetched.prediction='{1'b1,pc,predicted_target,1'b0,action};
    expected[index].fetched.speculated_ras_action=action;
    expected[index].fetched.direction=direction_metadata(pc,first,1);
    first_token=expected[index].fetched;
    second_token='0;
    second_token.pc=successor_pc; second_token.instruction=imm(12,0,42);
    second_token.raw_instruction=second_token.instruction; second_token.sequential_pc=successor_pc+4;
    second_token.direction=direction_metadata(successor_pc,second_token.instruction);
    offered_direction[pc]=first_token.direction;
    offered_direction[successor_pc]=second_token.direction;
    if(keep_successor) expect_instruction(successor_pc,second_token.instruction);
    instructions='{1'b1,'{2'd2,'{second_token,first_token}}};
    #1; while(!ready) tick(); tick(); instructions.valid=0;
  endtask
  task automatic stop_at(logic [63:0] pc, logic [63:0] target, int disposition = 0, logic [63:0] cause = 0, logic [63:0] value = 0);
    redirect_t item;
    item = '{pc: pc, target: disposition == 1 ? trap_target : target, resolution: '{guest:'0,disposition: 2'(disposition), cause: cause, value: value}};
    expected_redirects.push_back(item);
  endtask
  task automatic reset_core;
    canceled += response_count;
    reset = 1; instructions = '0; inject_enable = 0; inject_pc = 0; inject_result = '0;
    interrupts = 0; time_counter=123; trap_target = 0; reservation_valid=0;
    block_requests = 0; block_stores = 0; inject_memory_fault = 0; hold_responses = 0; lookup_mode = 0;
    hold_split=0; split_fault=0; expected_split_locality=0;
    expected.delete(); expected_redirects.delete(); expected_requests.delete(); expected_completions.delete(); response_owners.delete(); multiply_authorized_cycle.delete();
    expected_branch_taken.delete();
    training_pending.delete();
    for (int i = 0; i < 32; i++) model[i] = 0;
    for (int i = 0; i < 4096; i++) begin
      memory_bytes[i] = 8'(i ^ 'h98);
      model_bytes[i] = memory_bytes[i];
    end
    repeat (2) tick();
    reset = 0;
  endtask

  function automatic logic [31:0] csr(int f3, rd, rs1, address);
    return {12'(address), 5'(rs1), 3'(f3), 5'(rd), 7'h73};
  endfunction
  task automatic expect_system(logic [63:0] pc, logic [31:0] word, logic [63:0] value=0);
    retirement_t item;
    item='0; item.fetched.pc=pc; item.fetched.instruction=word; item.fetched.raw_instruction=word; item.fetched.sequential_pc=pc+4;
    item.fetched.direction=direction_metadata(pc,word);
    item.rd=word[11:7]; item.write=word[14:12]!=0 && item.rd!=0; item.data=value;
    if(item.write) model[item.rd]=value;
    expected.push_back(item);
  endtask
  task automatic csr_access(logic [63:0] pc, int f3, rd, rs1, address, logic [63:0] old_value);
    logic [31:0] word=csr(f3,rd,rs1,address);
    expect_system(pc,word,old_value);
    stop_at(pc,pc+4,3);
    send(pc,word,0,1,0,0);
    drain();
  endtask

  // Enter S through real M-mode CSR programming; no internal state pokes.
  task automatic supervisor_timer(input bit global_enable);
    logic [63:0] pc='h14000;
    time_counter=timer_compare-1;
    constant64(pc,1,timer_compare); drain();
    csr_access(pc,1,0,1,'h14d,0); pc+=4;
    constant64(pc,1,64'h8000000000000000); drain();
    csr_access(pc,1,0,1,'h30a,0); pc+=4;
    csr_access(pc,5,0,2,'h306,0); pc+=4;
    send(pc,imm(2,0,32),imm(3,0,'h700)); pc+=8; drain();
    csr_access(pc,1,0,2,'h303,0); pc+=4;
    csr_access(pc,1,0,2,'h304,0); pc+=4;
    csr_access(pc,1,0,3,'h105,0); pc+=4;
    constant64(pc,1,64'h800 | (global_enable ? 64'd2 : 64'd0)); drain();
    csr_access(pc,1,0,1,'h300,64'ha00000000); pc+=4;
    send(pc,imm(4,0,'h500),0,1); pc+=4; drain();
    csr_access(pc,1,0,4,'h341,0); pc+=4;
    expect_system(pc,32'h30200073); stop_at(pc,'h500,3);
    send(pc,32'h30200073,0,1,0,0); drain(); trap_target='h700;
  endtask

  initial begin
    reset_core();
    // Independent pairs must sustain full bandwidth rather than merely finish correctly.
    for (int i = 0; i < 20; i++) send(64'('h100 + i*8), imm(1, 0, i), imm(2, 0, -i));
    drain();
    assert (longest_dual_run >= 16) else $fatal(1, "dual-issue throughput lost: %0d", longest_dual_run);

    // Partial packets coalesce, and a dependency can forward from either EX lane.
    send('h200, imm(5, 0, 7), regop(6, 5, 5));
    send('h208, imm(7, 0, 19), regop(8, 7, 6));
    send('h210, imm(9, 0, -1), imm(10, 0, 31));
    send('h218, regop(11, 9, 10, 5, 32), regop(12, 10, 9));
    drain();
    assert (saw_repacked) else $fatal(1, "partial packet did not coalesce with next packet");

    // Same-group WAW pairs; youngest of several older writers wins subsequent RAW.
    begin
      int before_waw;
      before_waw=waw_dual;
      send('h2f0,imm(15,0,1),imm(15,0,2));
      send('h2f8,imm(16,15,0),imm(17,15,1)); drain();
      assert(waw_dual==before_waw+1) else $fatal(1,"normal WB WAW pair was split");
      // Read again after all forwarding candidates have drained: younger won RF.
      send('h2fc,imm(16,15,0),imm(17,15,1)); drain();
    end
    send('h300, imm(15, 0, 1), imm(15, 0, 2));
    send('h308, imm(15, 0, 3), imm(16, 15, 1));
    send('h310, imm(0, 15, 9), imm(17, 0, 4));
    drain();

    // Every selected ALU family, including six-bit shifts and RV64 word sign extension.
    send('h400, {20'h80000, 5'd18, 7'h37}, {20'hfffff, 5'd19, 7'h17});
    for (int f3 = 0; f3 < 8; f3++) begin
      send(64'('h408 + f3*16), imm(20, 18, f3 == 1 || f3 == 5 ? 40 : -7, f3), regop(21, 18, 19, f3));
      send(64'('h410 + f3*16), regop(22, 18, 19, f3 == 0 ? 0 : 5, 32), imm(23, 18, 'h428, 5));
    end
    send('h500, imm(24, 18, -1, 0, 'h1b), regop(25, 18, 19, 0, 0, 'h3b));
    send('h508, imm(24, 18, 7, 1, 'h1b), regop(25, 18, 19, 1, 0, 'h3b));
    send('h510, imm(24, 18, 7, 5, 'h1b), regop(25, 18, 19, 5, 0, 'h3b));
    send('h518, imm(24, 18, 'h407, 5, 'h1b), regop(25, 18, 19, 5, 32, 'h3b));
    send('h520, regop(24, 18, 19, 0, 32, 'h3b), imm(25, 24, 1));
    drain();

    // Seeded dependency-heavy arithmetic checks against an independent sequential model.
    begin
      logic [31:0] random_state;
      logic [31:0] words[2];
      random_state = 32'h126789ab;
      for (int i = 0; i < 160; i++) begin
        for (int lane = 0; lane < 2; lane++) begin
          random_state = random_state * 32'd1664525 + 32'd1013904223;
          words[lane] = regop(int'(random_state[4:0]), int'(random_state[9:5]), int'(random_state[14:10]), int'(random_state[17:15]));
        end
        send(64'('h1000 + 8*i), words[0], words[1]);
      end
    end
    drain();

    reset_core();
    // Both branch positions; younger already-issued and queued work is discarded.
    stop_at('h2000, 'h2040);
    send('h2000, jump(1, 64), imm(2, 0, 99), 2, 1, 0);
    send('h2008, imm(3, 0, 99), imm(4, 0, 99), 2, 0, 0);
    drain();
    send('h2040, regop(5, 2, 3), imm(6, 1, 0));
    stop_at('h204c, 'h208c);
    send('h2048, imm(7, 0, 7), jump(8, 64));
    send('h2050, imm(9, 0, 99), imm(10, 0, 99), 2, 0, 0);
    drain();
    send('h208c, regop(11, 9, 10), regop(12, 7, 8));
    drain();

    // All branch predicates, taken and not taken, with negative versus positive operands.
    send('h2100, imm(1, 0, -1), imm(2, 0, 1));
    drain();
    for (int f3 = 0; f3 < 8; f3++) begin
      if (f3 != 2 && f3 != 3) begin
        bit taken;
        taken = f3 == 1 || f3 == 4 || f3 == 7;
        if (taken) stop_at(64'('h2200 + 16*f3), 64'('h2220 + 16*f3));
        send(64'('h2200 + 16*f3), branch(1, 2, 32, f3), imm(3, 0, f3), 2, 1, !taken);
        drain();
        if (!taken) stop_at(64'('h2304 + 16*f3), 64'('h2324 + 16*f3));
        send(64'('h2300 + 16*f3), imm(4, 0, f3), branch(2, 1, 32, f3 == 0 ? 1 : f3 == 1 ? 0 : f3));
        drain();
      end
    end
    // Correct taken predictions retain a paired target instruction. Wrong
    // direction/target predictions kill that peer and recover exactly once.
    send_predicted('h2600,jump(0,32),'h2620,'h2620); drain();
    send_predicted('h2640,jump(1,32),'h2660,'h2660,1,2'd1); drain();
    stop_at('h2680,'h26a0);
    send_predicted('h2680,jump(0,32),'h26c0,'h26c0,0); drain();
    stop_at('h26e0,'h26e4);
    send_predicted('h26e0,branch(1,2,32),'h2700,'h2700,0); drain();
    // Next-PC accuracy alone cannot detect this direction mismatch: the taken
    // target equals fallthrough, but history must still append the actual one.
    stop_at('h2720,'h2724);
    send('h2720,branch(0,0,4),imm(3,0,99),2,1,0); drain();
    // JALR clears bit zero, and link data is available to the restarted stream.
    send('h2400, imm(5, 0, 'h601), imm(6, 0, 9));
    drain();
    stop_at('h2408, 'h600);
    send('h2408, imm(7, 5, 0, 0, 'h67), imm(8, 0, 99), 2, 1, 0);
    drain();
    send('h600, imm(8, 7, 0), imm(9, 0, 10));
    drain();
    stop_at('h24d4, 'h24e4);
    send('h24d0, branch(1, 2, 32), branch(1, 2, 16, 1));
    drain();

    // C permits halfword-aligned jump targets; only odd instruction PCs fault.
    stop_at('h2500, 'h2502);
    send('h2500, jump(10, 2), imm(11, 0, 99), 2, 1, 0);
    drain();
    stop_at('h2510, 'h2510, 1, 2, 64'hffffffff);
    send('h2510, 32'hffffffff, imm(11, 0, 99), 2, 0, 0);
    drain();
    stop_at('h2530, 'h602);
    send('h2530, imm(10, 5, 2, 0, 'h67), '0, 1);
    drain();
    stop_at('h2543, 'h2543, 1, 0, 'h2543);
    send('h2543, imm(10, 0, 1), '0, 1, 0, 0);
    drain();
    inject_enable = 1; inject_pc = 'h2550;
    inject_result = '{guest:'0,disposition: 2'd1, cause: 64'd5, value: 64'hdead};
    stop_at('h2550, 'h2550, 1, 2, 64'hffffffff);
    send('h2550, 32'hffffffff, '0, 1, 0, 0);
    drain(); inject_enable = 0;
    for (int lane = 0; lane < 2; lane++) begin
      for (int action = 1; action <= 2; action++) begin
        logic [63:0] pc;
        pc = 64'('h2600 + 64*lane + 16*action);
        inject_enable = 1; inject_pc = pc + 64'(4*lane);
        inject_result = '{guest:'0,disposition: 2'(action), cause: 64'd13, value: 64'h3000};
        stop_at(inject_pc, inject_pc, action, 13, 'h3000);
        send(pc, imm(12, 0, 101), imm(13, 0, 102), 2, lane == 1, 0);
        if (action == 1) tick();
        send(pc+8, imm(14, 12, 1), imm(15, 13, 1), 2, 0, 0);
        if (action == 1) begin
          #1;
          assert (issued == 2'(lane)) else $fatal(1, "unavailable MEM producer did not block its dependent");
        end
        drain();
        inject_enable = 0;
        if (action == 2) begin
          if (lane == 0) send(pc, imm(12, 0, 101), imm(13, 0, 102));
          else send(pc+4, imm(13, 0, 102), '0, 1);
          drain();
        end
        send(pc+12, imm(14, 12, 0), imm(15, 13, 0));
        drain();
      end
    end

    // An older injected fault wins over a younger taken branch in the same pair.
    inject_enable = 1; inject_pc = 'h2800;
    inject_result = '{guest:'0,disposition: 2'd1, cause: 64'd5, value: 64'hdead};
    stop_at('h2800, 'h2800, 1, 5, 'hdead);
    send('h2800, imm(16, 0, 1), jump(17, 64), 2, 0, 0);
    drain(); inject_enable = 0;

    // Reset discards queued/pipelined work without a late write or retirement.
    send('h2900, imm(20, 0, 99), imm(21, 0, 99), 2, 0, 0);
    tick();
    reset_core();
    drain();
    send('h3000, imm(22, 20, 0), imm(23, 21, 0));
    drain();
    assert (single_issues > 10 && stops == 23) else $fatal(1, "insufficient hazard/stop coverage: single=%0d stops=%0d", single_issues, stops);

    // Warm loads use the normal MEM/WB path and sustain one LSU plus one ALU each cycle.
    reset_core(); lookup_mode = 1;
    send('h4000, imm(1, 0, 'h300), imm(2, 0, -128)); drain();
    dual_run = 0; longest_dual_run = 0;
    for (int i = 0; i < 20; i++) send(64'('h4010 + i*8), imm(10+i, 1, (i%8)*8, 3, 'h03), imm(30, 0, i));
    drain();
    assert (longest_dual_run >= 16) else $fatal(1, "warm memory serialized independent work");

    // Every natural byte lane, store mask, signed/unsigned load width, and both age slots.
    for (int width = 0; width < 4; width++) begin
      for (int offset = 0; offset < 8; offset += 1 << width) begin
        send(64'('h4200 + width*128 + offset*8), store(2, 1, offset, width), imm(3, 0, offset)); drain();
        send(64'('h4500 + width*128 + offset*8), imm(4, 0, width), imm(5, 1, offset, width, 'h03)); drain();
        send('h4780, imm(6, 1, offset, width, 'h03), imm(7, 6, 1)); drain();
        if (width < 3) begin
          send('h4790, imm(8, 1, offset, width+4, 'h03), '0, 1); drain();
        end
      end
    end
    send('h4800, imm(3, 0, 3), store(2, 1, -8, 3)); drain();
    send('h4808, imm(4, 1, -8, 3, 'h03), imm(5, 1, 0, 3, 'h03)); drain();
    send('h4810, imm(0, 1, 0, 3, 'h03), imm(6, 0, 7)); drain();

    // Several accepted loads outlive WB; responses reserve younger issue while the older lane continues.
    lookup_mode = 0; configured_delay = 4; hold_responses = 1;
    for (int i = 0; i < 4; i++) send(64'('h4900+i*8), imm(10+i, 1, i*8, 3, 'h03), imm(20+i, 0, i));
    repeat (8) tick();
    assert (response_count == 4) else $fatal(1, "did not admit multiple misses");
    for (int i = 0; i < 16; i++) begin
      if (i == 8) hold_responses = 0;
      send(64'('h4940+i*8), imm(25, 0, i), imm(26, 0, -i));
    end
    drain();
    assert (max_outstanding == 4 && overlap_retirements > 8 && shared_writes > 0 && reserved_slots >= 4) else $fatal(1, "missing nonblocking/shared writeback coverage: outstanding=%0d overlap=%0d shared=%0d", max_outstanding, overlap_retirements, shared_writes);

    // A hit may bypass an older outstanding miss; RAW and WAW must still wait for its owner.
    hold_responses = 1;
    send('h4a00, imm(10, 1, 0, 3, 'h03), imm(20, 0, 1));
    repeat (6) tick(); lookup_mode = 1;
    send('h4a08, imm(11, 1, 8, 3, 'h03), imm(21, 0, 2));
    repeat (6) tick();
    assert (expected.size() == 0 && response_count == 1) else $fatal(1, "hit did not pass miss");
    send('h4a10, imm(12, 10, 1), imm(13, 0, 3));
    repeat (6) begin tick(); assert (issued == 0) else $fatal(1, "RAW escaped scoreboard"); end
    hold_responses = 0; drain();
    lookup_mode = 0; hold_responses = 1;
    send('h4a20, imm(10, 1, 0, 3, 'h03), imm(20, 0, 1));
    repeat (6) tick();
    send('h4a28, imm(10, 0, 91), imm(13, 0, 3));
    repeat (6) begin tick(); assert (issued == 0) else $fatal(1, "WAW escaped scoreboard"); end
    hold_responses = 0; drain();
    send('h4a30, imm(14, 10, 0), '0, 1); drain();

    // FIFO capacity failure replays only the unaccepted instruction; prior owners survive.
    hold_responses = 1;
    for (int i = 0; i < 4; i++) send(64'('h4b00+i*8), imm(10+i, 1, i*8, 3, 'h03), imm(20+i, 0, i));
    repeat (8) tick();
    stop_at('h4b44, 'h4b44, 2);
    send('h4b40, imm(24, 0, 42), imm(14, 1, 32, 3, 'h03), 2, 1, 0);
    repeat (8) tick();
    assert (expected_redirects.size() == 0 && response_count == 4) else $fatal(1, "capacity replay lost older owners");
    hold_responses = 0; drain();
    send('h4b44, imm(14, 1, 32, 3, 'h03), '0, 1); drain();

    // WB slow-request and hit-store rejection replay, with no accepted request or mutation.
    for (int lane = 0; lane < 2; lane++) begin
      logic [63:0] pc;
      pc = 64'('h4c00 + lane*32);
      block_requests = 1;
      stop_at(pc+64'(lane*4), pc+64'(lane*4), 2);
      if (lane == 0) send(pc, imm(15, 1, 0, 3, 'h03), imm(16, 0, 99), 2, 0, 0);
      else send(pc, imm(16, 0, 8), store(2, 1, 0, 3), 2, 1, 0);
      drain(); block_requests = 0;
      if (lane == 0) send(pc, imm(15, 1, 0, 3, 'h03), imm(16, 0, 99));
      else send(pc+4, store(2, 1, 0, 3), '0, 1);
      drain();
    end
    lookup_mode = 1; block_stores = 1;
    stop_at('h4c84, 'h4c84, 2);
    send('h4c80, imm(16, 0, 8), store(2, 1, 0, 3), 2, 1, 0); drain();
    block_stores = 0; send('h4c84, store(2, 1, 0, 3), '0, 1); drain();

    // Lookup replay/translation faults and admission access faults never issue slow work.
    for (int mode = 3; mode <= 5; mode++) begin
      for (int lane = 0; lane < 2; lane++) begin
        logic [63:0] pc;
        int cause;
        pc = 64'('h4d00+mode*32+lane*8);
        lookup_mode = mode;
        cause = mode == 4 ? (lane == 0 ? 13 : 15) : (lane == 0 ? 5 : 7);
        stop_at(pc+64'(lane*4), pc+64'(lane*4), mode == 3 ? 2 : 1, 64'(cause), 'h300);
        if (lane == 0) send(pc, imm(0, 1, 0, 3, 'h03), imm(16, 0, 99), 2, 0, 0);
        else send(pc, imm(16, 0, 8), store(2, 1, 0, 3), 2, 1, 0);
        drain();
      end
    end
    lookup_mode = 0; inject_memory_fault = 1; fault_address = 'h300;
    stop_at('h4e00, 'h4e00, 1, 5, 'h300);
    send('h4e00, imm(0, 1, 0, 3, 'h03), imm(16, 0, 99), 2, 0, 0); drain();
    stop_at('h4e14, 'h4e14, 1, 7, 'h300);
    send('h4e10, imm(16, 0, 8), store(2, 1, 0, 3), 2, 1, 0); drain();
    inject_memory_fault = 0;

    // Deferred responses use the same lane extraction and sign extension as hits.
    for (int f3 = 0; f3 < 7; f3++) begin
      int offset;
      offset = f3 == 0 || f3 == 4 ? 7 : f3 == 1 || f3 == 5 ? 6 : f3 == 3 ? 0 : 4;
      send(64'('h4e20+f3*8), imm(15, 1, offset, f3, 'h03), imm(16, 15, 1)); drain();
    end
    send('h4e90, imm(0, 1, 0, 3, 'h03), store(2, 1, 7, 0)); drain();

    // Split owners retire once after completion, preserving an older peer and
    // refetching younger work without adding a third RF write port.
    begin
      int before_splits;
      before_splits=split_requests;
      for (int width = 1; width < 4; width++) begin
        stop_at('h4e40, 'h4e44, 3);
        send('h4e40, imm(0, 1, 1, width, 'h03), imm(16, 0, 99), 2, 1, 0); drain();
        stop_at('h4e54, 'h4e58, 3);
        send('h4e50, imm(16, 0, 8), store(2, 1, 1, width)); drain();
        stop_at('h4e60, 'h4e64, 3);
        send('h4e60, imm(17, 1, 1, width, 'h03), 0, 1); drain();
        send('h4e64, imm(18,17,1), 0, 1); drain();
      end
      assert(split_requests==before_splits+9) else $fatal(1,"split accepted more than once");
      // The late outcome carries the exact second-fragment fault address.
      split_fault=1; stop_at('h4e74,'h4e74,1,13,'h304);
      send('h4e70,imm(16,0,11),imm(17,1,1,3,'h03),2,1,0); drain();
      split_fault=0;
      hold_responses=1; before_splits=split_requests;
      send('h4e80,imm(19,1,0,3,'h03),0,1);
      stop_at('h4e84,'h4e88,3);
      send('h4e84,imm(0,1,7,3,'h03),imm(16,0,99),2,1,0);
      repeat(14) tick();
      assert(response_count==1 && split_requests==before_splits) else $fatal(1,"split escaped older drain");
      hold_responses=0; drain();
      before_splits=split_requests;
      stop_at('h4e88,'h4ea8);
      send('h4e88,jump(0,32),store(2,1,7,3),2,1,0); drain();
      assert(split_requests==before_splits) else $fatal(1,"branch-killed split store issued");
    end

    // LR uses load fault classes; SC/AMO use store classes, including x0.
    // Both issue positions preserve a successful older peer and suppress younger work.
    for(int op=0;op<3;op++) for(int width=2;width<=3;width++) for(int lane=0;lane<2;lane++) begin
      logic [31:0] word;
      logic [63:0] pc;
      int operation, cause;
      operation=op==0 ? 2 : op==1 ? 3 : 0;
      word=atomic_insn(operation,width,0,1,op==0 ? 0 : 2);
      pc=64'('hb000+op*128+width*32+lane*8);
      inject_memory_fault=1; fault_address='h300;
      stop_at(pc+64'(4*lane),pc,1,op==0 ? 5 : 7,'h300);
      send(pc,lane==0 ? word : imm(16,0,8),lane==0 ? imm(16,0,99) : word,2,lane==1,0);
      drain(); inject_memory_fault=0;
      send(pc+8,imm(1,1,1),0,1); drain();
      cause=op==0 ? 4 : 6;
      stop_at(pc+16+64'(4*lane),pc,1,64'(cause),'h301);
      send(pc+16,lane==0 ? word : imm(16,0,8),lane==0 ? imm(16,0,99) : word,2,lane==1,0);
      drain();
      send(pc+24,imm(1,1,-1),0,1); drain();
    end
    // A successful older LR retains its owner and GPR result until drain,
    // even when the younger peer faults and flushes the speculative pipeline.
    configured_delay=20;
    stop_at('hb384,'hb384,1,2,64'hffffffff);
    send('hb380,atomic_insn(2,2,17,1),32'hffffffff,2,1,0);
    drain(); configured_delay=8;
    send('hb388,imm(18,17,0),0,1); drain();
    // Taken older branches kill younger atomics before any memory authorization.
    for(int op=0;op<3;op++) begin
      logic [63:0] pc;
      pc=64'('hb400+op*16);
      stop_at(pc,pc+32);
      send(pc,jump(16,32),atomic_insn(op==0 ? 2 : op==1 ? 3 : 0,3,0,1,op==0 ? 0 : 2),2,1,0);
      drain();
    end

    // An older stop prevents a younger store, including a speculative owned-store candidate.
    lookup_mode = 1;
    stop_at('h4f00, 'h4f20);
    send('h4f00, jump(16, 32), store(2, 1, 0, 3), 2, 1, 0); drain();
    inject_enable = 1; inject_pc = 'h4f40; inject_result = '{guest:'0,disposition: 2'd1, cause: 64'd2, value: 64'hdead};
    stop_at('h4f40, 'h4f40, 1, 2, 'hdead);
    send('h4f40, imm(16, 0, 9), store(2, 1, 0, 3), 2, 0, 0); drain(); inject_enable = 0;

    // A younger branch flush must not cancel its older accepted load.
    lookup_mode = 0; hold_responses = 1;
    stop_at('h5004, 'h5044);
    send('h5000, imm(10, 1, 0, 3, 'h03), jump(17, 64));
    repeat (10) tick();
    assert (expected_redirects.size() == 0 && response_count == 1) else $fatal(1, "branch canceled committed load");
    send('h5044, imm(18, 0, 5), imm(19, 0, 6));
    repeat (6) tick(); hold_responses = 0; drain();

    // Return and branch enter RR together: MEM recovery preserves the reserved completion.
    hold_responses = 1;
    send('h5080, imm(10, 1, 0, 3, 'h03), '0, 1);
    repeat (8) tick();
    stop_at('h5088, 'h50c8);
    send('h5088, jump(17, 64), imm(20, 0, 99), 2, 1, 0);
    hold_responses = 0; drain();

    // A younger fault drains older accepted work, including same-group WB acceptance.
    hold_responses = 1;
    inject_enable = 1; inject_pc = 'h5104; inject_result = '{guest:'0,disposition: 2'd1, cause: 64'd2, value: 64'hbad};
    stop_at('h5104, 'h5104, 1, 2, 'hbad);
    send('h5100, imm(11, 1, 8, 3, 'h03), imm(20, 0, 7), 2, 1, 0);
    repeat (10) tick();
    assert (expected_redirects.size() == 1 && response_count == 1 && expected.size() == 0) else $fatal(1, "fault did not retain precise drain boundary");
    hold_responses = 0; drain(); inject_enable = 0;

    // Fetch faults ignore even valid memory encodings and preserve both age slots.
    // In particular, a younger fault must wait for its older accepted load.
    begin
      int before_lookups;
      before_lookups = lookups;
      stop_at('h5140, 'h5140, 1, 1, 'h5140);
      send('h5140, imm(11, 1, 0, 3, 'h03), '0, 1, 0, 0, 0);
      drain();
      assert (lookups == before_lookups) else $fatal(1, "fetch fault caused a data lookup");
    end
    hold_responses = 1;
    stop_at('h5184, 'h5184, 1, 1, 'h5184);
    send('h5180, imm(11, 1, 8, 3, 'h03), store(2, 1, 0, 3), 2, 1, 0, 1);
    repeat (10) tick();
    assert (expected_redirects.size() == 1 && response_count == 1 && expected.size() == 0)
      else $fatal(1, "fetch fault did not wait for older load");
    hold_responses = 0; drain();

    // An older WB fault overrides a younger MEM branch on the same edge.
    stop_at('h51a0, 'h51a0, 1, 2, 64'hffffffff);
    send('h51a0, 32'hffffffff, '0, 1, 0, 0);
    send('h51a4, jump(17, 64), '0, 1, 0, 0);
    drain();
    assert(wb_overrides > 0) else $fatal(1, "missing simultaneous WB/MEM recovery");

    // Same-group WB authorization can override the prior cycle's MEM branch.
    // No link write or retirement survives the older memory replay/fault.
    for (int mode = 0; mode < 2; mode++) begin
      logic [63:0] pc;
      pc = 64'('h51c0 + mode*16);
      lookup_mode = 0;
      block_requests = mode == 0;
      inject_memory_fault = mode == 1; fault_address = 0;
      stop_at(pc+4, pc+68);
      stop_at(pc, pc, mode == 0 ? 2 : 1, 5, 0);
      send(pc, imm(11, 0, 0, 3, 'h03), jump(17, 64), 2, 0, 0);
      drain();
      block_requests = 0; inject_memory_fault = 0;
    end
    assert(mem_branch_redirects > 10) else $fatal(1, "missing early branch coverage");
    assert(minimum_instruction_capacity == 0) else $fatal(1, "instruction buffer never filled under issue backpressure");

    // Reset is an epoch boundary for both core owners and the memory service.
    hold_responses = 1; send('h5200, imm(12, 1, 0, 3, 'h03), '0, 1);
    repeat (8) tick(); assert (response_count == 1) else $fatal(1, "missing reset owner");
    reset_core(); drain();
    send('h5210, imm(13, 12, 1), imm(14, 0, 1)); drain();
    assert (stores > 10 && hits > 10 && stall_cycles >= 2) else $fatal(1, "missing memory scenarios");
    assert (requests == responses + canceled && canceled == 1) else $fatal(1, "lost/duplicate completion or reset ownership");

    // An unaligned CBO owns its response across branch recovery and blocks
    // younger memory, but does not reserve or write any integer destination.
    reset_core();
    send('h5800,imm(1,0,67),imm(2,0,7)); drain();
    hold_responses=1;
    send('h5808,32'h0040a00f,imm(3,0,9));
    repeat(10) tick();
    assert(response_count==1 && expected.size()==0) else $fatal(1,"CBO missing committed owner");
    stop_at('h5810,'h5850);
    send('h5810,jump(4,64),32'h0040a00f,2,1,0); repeat(6) tick();
    stop_at('h5850,'h5850,2);
    send('h5850,imm(5,1,-3,3,'h03),0,1,0,0);
    repeat(10) tick();
    assert(response_count==1 && expected_redirects.size()==0) else $fatal(1,"younger load passed CBO");
    hold_responses=0; drain();
    send('h5850,imm(5,1,-3,3,'h03),0,1); drain();

    // Shared xenvcfg permission follows current privilege, independently of
    // MPRV. Denied CBOs trap before any memory request with the instruction TVAL.
    for(int mode=0;mode<2;mode++) for(int machine_enable=0;machine_enable<2;machine_enable++) for(int supervisor_enable=0;supervisor_enable<2;supervisor_enable++) begin
      logic [63:0] pc;
      bit allowed;
      reset_core(); pc='h5900; allowed=machine_enable!=0 && (mode==1 || supervisor_enable!=0);
      constant64(pc,1,67); constant64(pc,2,'h500); constant64(pc,3,64'(mode)<<11); constant64(pc,4,128);
      if(machine_enable!=0) csr_access(pc,1,0,4,'h30a,0);
      pc+=4;
      if(supervisor_enable!=0) csr_access(pc,1,0,4,'h10a,0);
      pc+=4;
      csr_access(pc,1,0,3,'h300,64'ha00000000); pc+=4;
      csr_access(pc,1,0,2,'h341,0); pc+=4;
      expect_system(pc,32'h30200073); stop_at(pc,'h500,3);
      send(pc,32'h30200073,0,1,0,0); drain();
      if(allowed) begin send('h500,32'h0040a00f,0,1); drain(); end
      else begin
        stop_at('h500,0,1,2,64'h0040a00f);
        send('h500,32'h0040a00f,0,1,0,0); drain();
        csr_access('h600,2,6,0,'h343,64'h0040a00f);
      end
    end
    // Physical admission faults preserve original rs1, not the cache-block base.
    reset_core(); send('h5a00,imm(1,0,67),0,1); drain();
    inject_memory_fault=1; fault_address=67;
    stop_at('h5a04,0,1,7,67); send('h5a04,32'h0040a00f,0,1,0,0); drain();
    inject_memory_fault=0; csr_access('h5a08,2,6,0,'h343,67);

    // Management retains WB through acceptance and delayed completion. Either
    // lane can own it; only an older peer retires before the response.
    for(int operation=0;operation<3;operation++) for(int slot=0;slot<2;slot++) begin
      logic [31:0] cmo;
      logic [63:0] pc;
      int before_requests;
      reset_core(); pc='h5b00; cmo=(32'(operation)<<20)|32'h0000a00f;
      send(pc,imm(1,0,67),imm(2,0,7)); drain(); pc+=8;
      before_requests=requests; block_requests=1; hold_responses=1;
      stop_at(pc+4*64'(slot),pc+4*64'(slot+1),3);
      if(slot==0) send(pc,cmo,imm(3,0,9),2,1,0);
      else send(pc,imm(3,0,9),cmo,2);
      repeat(12) tick();
      assert(expected.size()==1 && requests==before_requests) else $fatal(1,"CMO lost retained owner before acceptance");
      block_requests=0; repeat(14) tick();
      assert(requests==before_requests+1 && response_count==1 && expected.size()==1) else $fatal(1,"CMO repeated or retired before response");
      hold_responses=0; drain();
      send(pc+4*64'(slot+1),imm(4,1,-3,3,'h03),0,1); drain();
    end
    // An older branch kills each management operation before WB authorization.
    for(int operation=0;operation<3;operation++) begin
      int before_requests;
      reset_core(); send('h5b80,imm(1,0,67),0,1); drain();
      before_requests=requests;
      stop_at('h5b84,'h5bc4);
      send('h5b84,jump(2,64),(32'(operation)<<20)|32'h0000a00f,2,1,0); drain();
      assert(requests==before_requests) else $fatal(1,"wrong-path maintenance was authorized");
    end
    // Illegal operations and physical faults preserve the original instruction/VA.
    // CBIE=01 converts only INVAL; CBCFE remains independent. MPRV must not
    // apply lower-privilege xenvcfg restrictions to M-mode execution.
    for(int operation=0;operation<3;operation++) for(int mode=0;mode<3;mode++) for(int enabled=0;enabled<4;enabled++) begin
      logic [31:0] cmo;
      logic [63:0] pc;
      bit allowed;
      reset_core(); pc='h5c00; cmo=(32'(operation)<<20)|32'h0000a00f;
      if($test$plusargs("debug")) $display("CMO permission op=%0d mode=%0d enable=%0d cycle=%0d",operation,mode,enabled,cycles);
      allowed=mode==2 || enabled==1 || (operation==0 ? enabled==3 : enabled==2);
      constant64(pc,1,67); constant64(pc,2,'h500);
      constant64(pc,3,mode==2 ? (64'd1<<17)|(64'd1<<11) : 64'(mode)<<11);
      constant64(pc,4,enabled==1 ? 64'h50 : enabled==2 ? 64'h40 : enabled==3 ? 64'h30 : 0);
      csr_access(pc,1,0,4,'h30a,0); pc+=4;
      csr_access(pc,1,0,4,'h10a,0); pc+=4;
      csr_access(pc,1,0,3,'h300,64'ha00000000); pc+=4;
      if(mode<2) begin
        csr_access(pc,1,0,2,'h341,0); pc+=4;
        expect_system(pc,32'h30200073); stop_at(pc,'h500,3);
        send(pc,32'h30200073,0,1,0,0); drain(); pc='h500;
      end
      if(allowed) begin
        expect_instruction(pc,cmo);
        if(mode<2 && operation==0 && enabled==1) expected_requests[0].access=9;
        stop_at(pc,pc+4,3); send(pc,cmo,0,1,0,0); drain();
      end else begin
        stop_at(pc,0,1,2,64'(cmo)); send(pc,cmo,0,1,0,0); drain();
      end
    end
    for(int slot=0;slot<2;slot++) begin
      reset_core(); send('h5d00,imm(1,0,67),0,1); drain();
      inject_memory_fault=1; fault_address=67;
      stop_at('h5d04+4*64'(slot),0,1,7,67);
      if(slot==0) send('h5d04,32'h0010a00f,imm(2,0,4),2,0,0);
      else send('h5d04,imm(2,0,4),32'h0020a00f,2,1,0);
      drain(); inject_memory_fault=0; csr_access('h5d10,2,6,0,'h343,67);
    end
    // An interrupt arriving after acceptance waits for the maintenance owner
    // to retire, then uses its successor rather than reissuing the CBO.
    reset_core(); send('h5e00,imm(1,0,67),imm(2,0,8)); drain();
    csr_access('h5e08,1,0,2,'h304,0);
    csr_access('h5e0c,1,0,2,'h300,64'ha00000000);
    hold_responses=1; stop_at('h5e10,'h5e14,3);
    send('h5e10,32'h0010a00f,0,1); repeat(12) tick();
    assert(response_count==1 && expected.size()==1) else $fatal(1,"CMO interrupt setup");
    stop_at('h5e14,0,3); interrupts=6'b010000; repeat(12) tick();
    assert(expected_redirects.size()==2 && expected.size()==1) else $fatal(1,"interrupt abandoned maintenance");
    hold_responses=0; drain(); interrupts=0;
    csr_access('h600,2,6,0,'h341,'h5e14);

    // Prefetch offsets exclude the selector, use rs1 forwarding, and never
    // acquire a load/store owner. Simultaneous hints retain dual retirement.
    reset_core(); send('h5f00,imm(1,0,33),imm(2,0,64)); drain();
    for(int op=0;op<4;op++) if(op!=2) for(int slot=0;slot<2;slot++) for(int variant=0;variant<4;variant++) begin
      int offset;
      logic [63:0] pc;
      offset=variant==0 ? -2048 : variant==1 ? -32 : variant==2 ? 0 : 2016;
      pc=64'('h5f08+op*128+slot*32+variant*8);
      if(slot==0) send(pc,prefetch_insn(op,1,offset),imm(3,0,9));
      else send(pc,imm(3,0,9),prefetch_insn(op,1,offset));
      drain();
    end
    send('h6200,prefetch_insn(0,1),prefetch_insn(3,2)); drain();
    send('h6208,imm(1,1,32),prefetch_insn(1,1,-32)); drain();
    send('h6210,imm(7,1,3,6),prefetch_insn(2,1)); drain(); // ORI neighbors
    stop_at('h6218,'h6258); send('h6218,jump(3,64),prefetch_insn(3,1),2,1,0); drain();
    inject_enable=1; inject_pc='h6260; inject_result='{guest:'0,disposition:2'd1,cause:64'd5,value:64'hbad};
    stop_at('h6260,0,1,5,'hbad); send('h6260,imm(4,0,1),prefetch_insn(1,1),2,0,0); drain(); inject_enable=0;
    assert(prefetch_count>=26 && prefetch_pairs>0) else $fatal(1,"missing prefetch issue/dual coverage");

    // Real CSR commands return the old value, preserve source-index write intent,
    // and serialize even without a GPR destination. Younger work is refetched.
    reset_core();
    send('h6000, imm(1,0,'h55), imm(2,0,0)); drain();
    csr_access('h6008,1,3,1,'h340,0);     // CSRRW mscratch <- x1
    csr_access('h600c,6,4,10,'h340,'h55); // CSRRSI adds bits 1 and 3
    csr_access('h6010,7,5,3,'h340,'h5f);  // CSRRCI removes bits 0 and 1
    csr_access('h6014,2,6,0,'h340,'h5c);  // rs1=x0 is read-only
    csr_access('h6018,3,7,2,'h340,'h5c);  // nonzero rs1 containing zero is still a write
    csr_access('h601c,5,0,0,'h340,'h5c);  // CSRRWI with zero clears, even rd=x0
    csr_access('h6020,2,8,0,'h340,0);
    csr_access('h6024,2,9,0,'hf14,7);     // read-only mhartid
    stop_at('h6028,0,1,2,64'(csr(2,10,2,'hf14)));
    send('h6028,csr(2,10,2,'hf14),0,1,0,0); drain();
    csr_access('h602c,2,11,0,'h343,64'(csr(2,10,2,'hf14)));

    // CSR WB recovery wins over a younger taken branch in MEM, and kills a
    // speculative store before authorization. All issue groups remain single-slot.
    begin
      int before_override;
      before_override=wb_overrides;
      expect_system('h6040,csr(5,12,19,'h340),0);
      stop_at('h6040,'h6044,3);
      send('h6040,csr(5,12,19,'h340),0,1,0,0);
      send('h6044,jump(13,64),store(1,0,0,3),2,0,0);
      drain();
      assert(wb_overrides>before_override) else $fatal(1,"CSR did not cover simultaneous MEM recovery");
      csr_access('h6048,2,14,0,'h340,19);
    end

    // Count the successful prefix: two ordinary instructions add two, a
    // younger fault adds only its older peer, and an older fault adds zero.
    reset_core();
    csr_access('h6100,1,0,0,'hb02,0); // explicit counter write wins over its own retirement
    send('h6104,imm(1,0,1),imm(2,0,2)); drain();
    csr_access('h610c,2,3,0,'hb02,2);
    stop_at('h6114,0,1,1,'h6114);
    send('h6110,imm(4,0,4),imm(5,0,5),2,1,0,1); drain();
    csr_access('h6118,2,6,0,'hb02,4);
    csr_access('h611c,2,7,0,'h341,'h6114);
    csr_access('h6120,2,8,0,'h342,1);
    csr_access('h6124,2,9,0,'h343,'h6114);
    stop_at('h6128,0,1,2,64'hffffffff);
    send('h6128,32'hffffffff,imm(10,0,10),2,0,0); drain();
    csr_access('h6130,2,11,0,'hb02,8);
    csr_access('h6134,2,12,0,'h341,'h6128);

    // Trap-vector/return state is architectural, not a controller stop.
    reset_core();
    send('h6200,imm(1,0,'h700),imm(2,0,'h400)); drain();
    csr_access('h6208,1,0,1,'h305,0); trap_target='h700;
    stop_at('h6210,0,1,11,0);
    send('h6210,32'h00000073,imm(15,0,99),2,0,0); drain();
    csr_access('h700,2,3,0,'h341,'h6210);
    csr_access('h704,2,4,0,'h342,11);
    csr_access('h708,2,5,0,'h343,0);
    csr_access('h70c,1,0,2,'h341,'h6210);
    expect_system('h710,32'h30200073); stop_at('h710,'h400,3);
    send('h710,32'h30200073,0,1,0,0); drain();
    stop_at('h400,0,1,3,0);
    send('h400,32'h00100073,0,1,0,0); drain();
    csr_access('h714,2,6,0,'h341,'h400);
    csr_access('h718,2,7,0,'h342,3);

    // MRET enters S, SRET enters U; denied CSR access traps back to M with the
    // original encoding. S-mode exception delegation uses stvec/sepc/scause.
    reset_core();
    send('h6300,imm(1,0,'h700),imm(2,0,'h740)); drain();
    csr_access('h6308,1,0,1,'h305,0); trap_target='h700;
    csr_access('h630c,1,0,2,'h105,0);
    send('h6310,imm(3,0,1),imm(4,0,'h500)); drain();
    send('h6318,imm(3,3,11,1),imm(5,0,'h540)); drain(); // x3 = MPP.S
    csr_access('h6320,1,0,3,'h300,64'ha00000000);
    csr_access('h6324,1,0,4,'h341,0);
    csr_access('h6328,1,0,5,'h141,0);
    expect_system('h632c,32'h30200073); stop_at('h632c,'h500,3);
    send('h632c,32'h30200073,0,1,0,0); drain();
    expect_system('h500,32'h10200073); stop_at('h500,'h540,3);
    send('h500,32'h10200073,0,1,0,0); drain();
    stop_at('h540,0,1,2,64'(csr(2,6,0,'h340)));
    send('h540,csr(2,6,0,'h340),0,1,0,0); drain();
    csr_access('h700,2,7,0,'h341,'h540);
    csr_access('h704,2,8,0,'h343,64'(csr(2,6,0,'h340)));
    csr_access('h708,5,0,8,'h302,0); // delegate breakpoint
    csr_access('h70c,1,0,3,'h300,64'ha00000000);
    csr_access('h710,1,0,4,'h341,'h540);
    expect_system('h714,32'h30200073); stop_at('h714,'h500,3);
    send('h714,32'h30200073,0,1,0,0); drain();
    trap_target='h740;
    stop_at('h500,0,1,3,0); send('h500,32'h00100073,0,1,0,0); drain();
    csr_access('h740,2,9,0,'h141,'h500);
    csr_access('h744,2,10,0,'h142,3);

    // An accepted load must finish and write its reserved port before the
    // following system command can read its source or mutate CSR state.
    reset_core(); hold_responses=1;
    send('h6400,imm(1,0,0,3,'h03),imm(2,0,2));
    expect_system('h6408,csr(1,3,1,'h340),0); stop_at('h6408,'h640c,3);
    send('h6408,csr(1,3,1,'h340),0,1,0,0);
    repeat(10) tick();
    assert(response_count==1 && expected.size()==1 && expected_redirects.size()==1) else $fatal(1,"CSR escaped accepted-load drain");
    hold_responses=0; drain();
    csr_access('h640c,2,4,0,'h340,model[1]);

    // Svinval uses the same precise, single-slot WB boundary. TVM restricts
    // invalidation, not the ordering instructions; U may execute neither.
    for(int privilege=0;privilege<3;privilege++) for(int tvm=0;tvm<2;tvm++) for(int operation=0;operation<3;operation++) begin
      logic [31:0] word;
      logic [63:0] pc;
      bit allowed;
      int before_flushes;
      reset_core(); pc='h16000;
      word=operation==0 ? 32'h17f08073 : (operation==1 ? 32'h18000073 : 32'h18100073);
      allowed=privilege==2 || (privilege==1 && (operation!=0 || tvm==0));
      constant64(pc,1,(tvm!=0 ? 64'h100000 : 0) | (privilege==1 ? 64'h800 : 0)); drain();
      csr_access(pc,1,0,1,'h300,64'ha00000000); pc+=4;
      if(privilege!=2) begin
        send(pc,imm(2,0,'h500),0,1); pc+=4; drain();
        csr_access(pc,1,0,2,'h341,0); pc+=4;
        expect_system(pc,32'h30200073); stop_at(pc,'h500,3);
        send(pc,32'h30200073,0,1,0,0); drain(); pc='h500;
      end
      before_flushes=translation_invalidations;
      // An ordinary older peer retires, while the system command issues alone.
      expect_instruction(pc,imm(6,0,42));
      if(allowed) begin expect_system(pc+4,word); stop_at(pc+4,pc+8,3); end
      else stop_at(pc+4,0,1,2,64'(word));
      send(pc,imm(6,0,42),word,2,0,0); drain();
      assert(translation_invalidations==before_flushes+int'(allowed && operation==0))
        else $fatal(1,"Svinval invalidation/ordering mismatch privilege=%0d TVM=%0d operation=%0d",privilege,tvm,operation);
      if(!allowed) begin
        csr_access(0,2,7,0,'h341,pc+4);
        csr_access(4,2,8,0,'h343,64'(word));
      end
    end
    for(int operation=0;operation<3;operation++) begin
      logic [31:0] word;
      int before_flushes;
      reset_core(); hold_responses=1;
      word=operation==0 ? 32'h16000073 : (operation==1 ? 32'h18000073 : 32'h18100073);
      before_flushes=translation_invalidations;
      send('h16100,imm(1,0,0,3,'h03),imm(2,0,2));
      expect_system('h16108,word); stop_at('h16108,'h1610c,3);
      send('h16108,word,imm(3,0,99),2,0,0);
      repeat(10) tick();
      assert(response_count==1 && expected.size()==1 && translation_invalidations==before_flushes)
        else $fatal(1,"Svinval escaped accepted-load drain");
      hold_responses=0; drain();
      assert(translation_invalidations==before_flushes+int'(operation==0))
        else $fatal(1,"Svinval repeated invalidation");
    end

    // Interrupts stop before the oldest unretired instruction and retain the
    // precise boundary while already retired load owners drain.
    reset_core();
    send('h6500,imm(1,0,'h700),imm(2,0,8)); drain();
    csr_access('h6508,1,0,1,'h305,0); trap_target='h700;
    csr_access('h650c,1,0,2,'h304,0);
    csr_access('h6510,1,0,2,'h300,64'ha00000000);
    hold_responses=1;
    send('h6514,imm(3,0,0,3,'h03),imm(4,0,4));
    repeat(8) tick();
    stop_at('h651c,'h700,3); interrupts=6'b010000;
    repeat(10) tick();
    assert(expected_redirects.size()==1 && response_count==1) else $fatal(1,"interrupt entered before load drain");
    hold_responses=0; drain(); interrupts=0;
    csr_access('h700,2,5,0,'h341,'h651c);
    csr_access('h704,2,6,0,'h342,64'h8000000000000003);
    csr_access('h708,2,7,0,'h343,0);
    // Return with MIE restored, then intercept a live pair at WB, neither retires.
    expect_system('h70c,32'h30200073); stop_at('h70c,'h651c,3);
    send('h70c,32'h30200073,0,1,0,0); drain();
    send('h651c,imm(8,0,8),imm(9,0,9),2,0,0);
    wait(memory_stage[0].valid); @(negedge clock);
    stop_at('h651c,'h700,3); interrupts=6'b010000;
    drain(); interrupts=0;
    csr_access('h710,2,10,0,'h341,'h651c);

    // Interrupts cannot abandon a noncancelable split after prefix dispatch.
    // Complete and retire it first, then trap at its sequential successor.
    reset_core();
    send('h6540,imm(1,0,'h301),imm(2,0,8)); drain();
    send('h6548,imm(3,0,'h700),0,1); drain();
    csr_access('h654c,1,0,3,'h305,0); trap_target='h700;
    csr_access('h6550,1,0,2,'h304,0);
    csr_access('h6554,1,0,2,'h300,64'ha00000000);
    hold_split=1; stop_at('h6558,'h655c,3);
    send('h6558,imm(4,1,0,1,'h03),0,1);
    wait(split_active); interrupts=6'b010000;
    repeat(16) tick();
    assert(expected.size()==1 && expected_redirects.size()==1) else $fatal(1,"interrupt abandoned pending split");
    stop_at('h655c,'h700,3); hold_split=0; drain(); interrupts=0;
    csr_access('h700,2,5,0,'h341,'h655c);

    // WFI retires once, blocks younger effects, and wakes on locally enabled
    // pending interrupts even with global MIE clear. With MIE set it traps.
    reset_core();
    csr_access('h6600,5,0,8,'h304,0);
    expect_system('h6604,32'h10500073);
    send('h6604,32'h10500073,imm(11,0,11),2,0,0); drain();
    assert(sleeping) else $fatal(1,"WFI did not sleep");
    stop_at('h6608,'h6608,3); interrupts=6'b010000;
    repeat(3) tick(); interrupts=0; drain();
    assert(!sleeping) else $fatal(1,"WFI failed local wake");
    csr_access('h6608,2,12,0,'hb02,2);
    send('h660c,imm(1,0,'h700),imm(2,0,8)); drain();
    csr_access('h6614,1,0,1,'h305,0); trap_target='h700;
    csr_access('h6618,1,0,2,'h300,64'ha00000000);
    expect_system('h661c,32'h10500073);
    send('h661c,32'h10500073,0,1,0,0); drain();
    stop_at('h6620,'h700,3); interrupts=6'b010000;
    drain(); interrupts=0;
    csr_access('h700,2,13,0,'h341,'h6620);
    assert(!sleeping) else $fatal(1,"interrupt left core asleep");

    // PAUSE is an exact FENCE overlay. It retires before its bounded cooldown,
    // without redirecting or discarding buffered successors, in either slot.
    for(int slot=0;slot<2;slot++) begin
      int before_pause, before_dual;
      reset_core(); before_pause=pause_commits; before_dual=pause_dual;
      if(slot==0) send('h6640,32'h0100000f,imm(1,0,7));
      else begin
        send('h6640,imm(1,0,7),32'h0100000f);
        send('h6648,imm(2,0,8),imm(3,0,9));
      end
      while(pause_commits==before_pause) tick();
      assert(issued==0 && !sleeping) else $fatal(1,"PAUSE did not begin cooldown");
      repeat(15) begin tick(); assert(issued==0) else $fatal(1,"PAUSE cooldown too short"); end
      tick(); assert(issued!=0) else $fatal(1,"PAUSE cooldown did not release issue");
      drain();
      assert(pause_commits==before_pause+1 && pause_dual==before_dual+slot)
        else $fatal(1,"PAUSE slot/retirement mismatch");
    end
    // Adjacent hints must not coissue across the older pause.
    begin
      int before_pause;
      reset_core(); before_pause=pause_commits;
      send('h6650,32'h0100000f,32'h0100000f); send('h6658,imm(1,0,1),0,1); drain();
      assert(pause_commits==before_pause+2) else $fatal(1,"consecutive PAUSE lost");
    end
    // Unlike FENCE, PAUSE does not drain an older accepted load, and its
    // cooldown does not obstruct the independent deferred completion path.
    begin
      int before_pause;
      reset_core(); before_pause=pause_commits; hold_responses=1;
      send('h6660,imm(5,0,0,3,'h03),32'h0100000f);
      while(pause_commits==before_pause) tick();
      assert(response_count==1) else $fatal(1,"PAUSE waited for prior memory");
      send('h6668,imm(1,0,7),0,1);
      repeat(3) tick(); hold_responses=0;
      drain(); assert(pause_commits==before_pause+1) else $fatal(1,"PAUSE retired twice");
    end
    // A killed hint must neither retire nor delay the recovered stream.
    for(int reason=0;reason<3;reason++) begin
      int before_pause, restart_cycle;
      reset_core(); before_pause=pause_commits;
      if(reason==0) begin
        stop_at('h6670,'h66b0); send('h6670,jump(0,64),32'h0100000f,2,1,0);
      end else begin
        inject_enable=1; inject_pc='h6670; inject_result='{guest:'0,disposition:2'(reason),cause:64'd5,value:64'hbad};
        stop_at('h6670,reason==1 ? 0 : 'h6670,reason,5,'hbad);
        send('h6670,imm(1,0,1),32'h0100000f,2,0,0);
      end
      drain(); inject_enable=0; restart_cycle=cycles;
      send('h66b0,imm(2,0,2),0,1); drain();
      assert(pause_commits==before_pause && cycles-restart_cycle<16) else $fatal(1,"killed PAUSE delayed recovery");
    end
    // Interrupts end cooldown promptly and report the already-retired hint's
    // successor. A later reset must likewise discard a pending cooldown.
    begin
      int before_pause, before_stop, interrupt_cycle;
      reset_core(); csr_access('h66c0,5,0,8,'h304,0); csr_access('h66c4,6,0,8,'h300,64'ha00000000);
      before_pause=pause_commits; send('h66c8,32'h0100000f,0,1);
      while(pause_commits==before_pause) tick();
      before_stop=stops; interrupt_cycle=cycles; stop_at('h66cc,0,3); interrupts=6'b010000;
      while(stops==before_stop) tick();
      assert(cycles-interrupt_cycle<8) else $fatal(1,"PAUSE blocked interrupt entry");
      interrupts=0; drain(); csr_access(0,2,3,0,'h341,'h66cc);
      reset_core(); before_pause=pause_commits; send('h66d0,32'h0100000f,0,1);
      while(pause_commits==before_pause) tick();
      reset_core(); interrupt_cycle=cycles; send('h66d4,imm(2,0,2),0,1); drain();
      assert(cycles-interrupt_cycle<16) else $fatal(1,"reset retained PAUSE cooldown");
    end

    // WRS has no register/memory effects, compacts from the younger slot, and
    // retires only once. Its younger speculative store is refetched, not committed.
    for(int short_wait=0;short_wait<2;short_wait++) begin
      logic [31:0] word;
      word=short_wait!=0 ? 32'h01d00073 : 32'h00d00073;
      reset_core();
      expect_instruction('h6700,imm(1,0,7)); expect_system('h6704,word);
      stop_at('h6704,'h6708,3);
      send('h6700,imm(1,0,7),word,2,0,0); drain();
      reservation_valid=1;
      expect_system('h6708,word); stop_at('h6708,'h670c,3);
      send('h6708,word,store(1,0,0,3),2,0,0);
      wait(sleeping); repeat(12) tick();
      assert(expected.size()==1 && expected_redirects.size()==1 && !memory_out.request.valid && !split_out.valid)
        else $fatal(1,"reservation wait escaped or dispatched memory");
      reservation_valid=0; drain();
      send('h670c,imm(2,1,1),0,1); drain();
    end
    // An accepted LR response and its RF write drain before WRS may start.
    reset_core(); hold_responses=1;
    send('h6720,atomic_insn(2,3,1,0),0,1); repeat(10) tick();
    expect_system('h6724,32'h00d00073); stop_at('h6724,'h6728,3);
    send('h6724,32'h00d00073,0,1,0,0); repeat(12) tick();
    assert(!sleeping && response_count==1 && expected.size()==1) else $fatal(1,"WRS started before older LR completion");
    reservation_valid=1; hold_responses=0; wait(sleeping); repeat(8) tick();
    assert(response_count==0 && expected_completions.size()==0) else $fatal(1,"WRS lost older completion");
    reservation_valid=0; drain();
    // Locally enabled interrupts wake with global MIE clear; enabled interrupts
    // trap at the successor after the retained WRS has retired.
    for(int global_enable=0;global_enable<2;global_enable++) begin
      reset_core(); csr_access('h6740,5,0,8,'h304,0);
      if(global_enable!=0) csr_access('h6744,6,0,8,'h300,64'ha00000000);
      reservation_valid=1;
      expect_system('h6748,32'h00d00073); stop_at('h6748,'h674c,3);
      send('h6748,32'h00d00073,0,1,0,0); wait(sleeping); repeat(8) tick();
      if(global_enable!=0) stop_at('h674c,0,3);
      interrupts=6'b010000; drain(); interrupts=0;
      assert(!sleeping) else $fatal(1,"WRS interrupt wake");
      if(global_enable!=0) csr_access(0,2,3,0,'h341,'h674c);
    end
    // U and S permit WRS even with TW set: STO completes normally after the
    // short bound, while NTO traps only when that bound expires. M ignores TW.
    for(int privilege=0;privilege<3;privilege++) for(int short_wait=0;short_wait<(privilege==0 ? 3 : 2);short_wait++) begin
      logic [63:0] pc, mstatus;
      logic [31:0] word;
      int start_cycle;
      word=short_wait==1 ? 32'h01d00073 : 32'h00d00073;
      reset_core(); pc='h100;
      mstatus=(64'd1<<21)|(64'(privilege==2 ? 3 : privilege)<<11);
      constant64(pc,1,mstatus); drain();
      csr_access(pc,1,0,1,'h300,64'ha00000000); pc+=4;
      if(privilege!=2) begin
        send(pc,imm(2,0,'h500),0,1); drain(); pc+=4;
        csr_access(pc,1,0,2,'h341,0); pc+=4;
        expect_system(pc,32'h30200073); stop_at(pc,'h500,3);
        send(pc,32'h30200073,0,1,0,0); drain();
      end
      reservation_valid=1;
      if(short_wait==0 && privilege!=2) stop_at('h500,0,1,2,64'(word));
      else begin expect_system('h500,word); stop_at('h500,'h504,3); end
      send('h500,word,0,1,0,0); wait(sleeping); start_cycle=cycles;
      repeat(32) tick();
      assert(sleeping && expected_redirects.size()==1) else $fatal(1,"WRS timed out prematurely");
      // Reservation loss on the timeout edge must win over TW's exception.
      if(short_wait==2) begin
        repeat(4063) tick();
        reservation_valid=0;
      end
      if(privilege==2 && short_wait==0) begin
        repeat(4100) tick();
        assert(sleeping && expected_redirects.size()==1) else $fatal(1,"M-mode NTO incorrectly timed out");
        reservation_valid=0;
      end
      drain();
      assert(cycles-start_cycle>=4095 && cycles-start_cycle<4300) else $fatal(1,"WRS bounded timeout");
      if(short_wait==0 && privilege!=2) begin
        csr_access(0,2,3,0,'h341,'h500);
        csr_access(4,2,4,0,'h343,64'(word));
      end
    end
    // STO also wakes on reservation loss. Reset discards a pending owner
    // without a delayed retirement in the next epoch.
    reset_core(); reservation_valid=1;
    expect_system('h6760,32'h01d00073); stop_at('h6760,'h6764,3);
    send('h6760,32'h01d00073,0,1,0,0); wait(sleeping); repeat(4094) tick();
    reservation_valid=0; drain();
    reservation_valid=1; send('h6764,32'h00d00073,0,1,0,0); wait(sleeping); repeat(8) tick();
    reset_core(); send('h6780,imm(3,0,3),imm(4,0,4)); drain();
    stop_at('h6788,'h67c8); send('h6788,jump(0,64),32'h00d00073,2,1,0); drain();
    assert(!sleeping) else $fatal(1,"killed WRS captured a wait owner");

    // Fences retain no WB bubble: they wait in RR for the accepted owner and
    // its deferred GPR write, issue alone, then flush younger branch recovery.
    for(int instruction_fence=0;instruction_fence<2;instruction_fence++) begin
      logic [31:0] word;
      reset_core(); hold_responses=1;
      word=instruction_fence!=0 ? 32'h0000100f : 32'h0ff0000f;
      send('h6800,imm(5,0,0,3,'h03),0,1); repeat(8) tick();
      expect_system('h6804,word); stop_at('h6804,'h6808,3);
      send('h6804,word,jump(0,64),2,0,0);
      repeat(12) tick();
      assert(response_count==1 && expected_redirects.size()==1 && invalidations==0) else $fatal(1,"fence escaped older drain");
      hold_responses=0; drain();
      assert(invalidations==instruction_fence) else $fatal(1,"FENCE.I invalidation count");
    end
    // Every M operation, including word projection, signed high products,
    // divide-by-zero, signed overflow, and independently varying operand signs.
    for(int scenario=0;scenario<6;scenario++) begin
      logic [63:0] pc;
      logic [63:0] a,b;
      pc='h8000;
      reset_core();
      case(scenario)
        0: begin a=64'hfedcba9876543210; b=64'h0123456789abcdef; end
        1: begin a=64'h8000000000000000; b='1; end
        2: begin a=64'hffffffff80000000; b=64'hffffffffffffffff; end
        3: begin a=64'h8000000080000001; b=0; end
        4: begin a=64'hffffffffffffffff; b=64'h8000000000000001; end
        5: begin a=64'h7fffffff7fffffff; b=7; end
      endcase
      constant64(pc,1,a); constant64(pc,2,b); drain();
      for(int word=0;word<2;word++) for(int funct3=0;funct3<8;funct3++) if(word==0 || funct3==0 || funct3>=4) begin
        send(pc,m_insn(3,1,2,funct3,word!=0),imm(4,0,funct3)); pc+=8;
        send(pc,imm(5,3,1),imm(6,4,1)); pc+=8;
        drain();
      end
      send(pc,m_insn(0,1,2,4),m_insn(0,1,2,0)); drain();
    end
    // Pipelined requests overlap each other and slow loads; response order is
    // allowed to differ from retirement. The second GPR port remains reserved.
    reset_core(); configured_delay=50;
    send('h9000,imm(1,0,-17),imm(2,0,7)); drain();
    send('h9008,imm(20,0,0,3,'h03),imm(21,0,21));
    for(int i=0;i<12;i++) send(64'('h9010+i*8),m_insn(3+i,1,2,i%4),imm(22,0,i));
    send('h9070,m_insn(17,1,2,4),imm(18,0,18));
    send('h9078,imm(19,3,1),imm(2,0,3)); drain(); configured_delay=8;
    // Accepted multiply/divide owners survive a younger trap and drain before
    // trap entry. A rejected owner must never complete or set the scoreboard.
    for(int funct3=0;funct3<=4;funct3+=4) begin
      reset_core(); send('h9200,imm(1,0,-101),imm(2,0,3)); drain();
      stop_at('h920c,'h920c,1,2,64'hffffffff);
      send('h9208,m_insn(3,1,2,funct3),32'hffffffff,2,1,0); drain();
      send('h9300,imm(4,3,0),imm(3,0,9)); drain();
      reset_core(); send('h9400,imm(1,0,101),imm(2,0,3)); drain();
      stop_at('h9408,'h9408,1,13,'hdead);
      inject_enable=1; inject_pc='h9408; inject_result='{guest:'0,disposition:2'd1,cause:64'd13,value:64'hdead};
      send('h9408,imm(7,0,7),m_insn(3,1,2,funct3),2,0,0); drain(); inject_enable=0;
      send('h9500,imm(3,0,5),imm(4,3,1)); drain();
    end
    // The busy divider replays before acceptance; the accepted owner survives.
    reset_core();
    begin
      logic [63:0] pc;
      pc='ha000;
      constant64(pc,1,64'h7fffffffffffffff); constant64(pc,2,3); drain();
    end
    send('ha100,m_insn(3,1,2,4),imm(8,0,8));
    stop_at('ha108,'ha108,2);
    send('ha108,m_insn(4,1,2,5),imm(9,0,9),2,0,0); drain();
    send('ha108,m_insn(4,1,2,5),imm(9,0,9)); drain();
    send('ha110,imm(5,3,1),imm(6,4,1)); drain();
    // Same-group older preacceptance replay rejects an EX-launched multiply.
    reset_core(); send('ha200,imm(1,0,17),imm(2,0,3)); drain();
    stop_at('ha208,'ha208,2);
    inject_enable=1; inject_pc='ha208; inject_result='{guest:'0,disposition:2'd2,cause:64'd0,value:64'd0};
    send('ha208,imm(7,0,7),m_insn(3,1,2,0),2,0,0); drain(); inject_enable=0;
    send('ha208,imm(7,0,7),m_insn(3,1,2,0)); drain();
    // A returned product writes immediately; even a read-modify-write consumer
    // can issue without a deferred WAW tail after the fixed arithmetic latency.
    reset_core(); send('hab00,imm(1,0,17),imm(2,0,3)); drain();
    send('hab08,m_insn(3,1,2,0),imm(8,0,8));
    send('hab10,imm(3,3,1),imm(5,3,2));
    send('hab18,imm(6,3,3),imm(7,3,4)); drain();
    assert(multiply_mem_cycle>=0 && dependent_mem_cycle-multiply_mem_cycle<=4)
      else $fatal(1,"multiply consumer waited beyond return: producer=%0d consumer=%0d",multiply_mem_cycle,dependent_mem_cycle);
    // More independent work than the arithmetic depth must still sustain one
    // direct write per cycle, without completion-buffer capacity admission.
    reset_core(); send('hac00,imm(1,0,17),imm(2,0,3)); drain();
    for(int pair=0;pair<8;pair++) send('hac08+64'(pair*8),m_insn(3+2*pair,1,2,0),m_insn(4+2*pair,1,2,0));
    drain(); assert(multiply_stream_count==16) else $fatal(1,"multiply stream lost a return");
    send('hac48,imm(20,3,1),imm(21,18,2)); drain();
    // A MEM branch kills a younger EX launch but accepted older work survives.
    reset_core(); send('h9600,imm(1,0,17),imm(2,0,3)); drain();
    stop_at('h9608,'h9700); send('h9608,jump(0,'hf8),m_insn(3,1,2,0),2,1,0); drain();
    send('h9700,imm(3,0,5),imm(4,3,1)); drain();
    csr_access('h9708,2,5,0,'h301,64'h8000000000141187);
    // Complete B catalog in both issue slots, with independent paired B work
    // and dependent consumers. Dirty upper words expose .UW/word/unary shaping.
    for(int scenario=0;scenario<7;scenario++) begin
      logic [63:0] pc;
      logic [63:0] a,b;
      int shift;
      pc='hb000;
      reset_core();
      case(scenario)
        0: begin a=0; b=0; shift=0; end
        1: begin a='1; b='1; shift=63; end
        2: begin a=64'h800000007fffffff; b=64'h8000000000000020; shift=32; end
        3: begin a=64'h0123456789abcdef; b=64'hfedcba987654321f; shift=31; end
        4: begin a=64'h8000000080000001; b=64'h8000000000000041; shift=1; end
        5: begin a=64'h8000000000000000; b=63; shift=63; end
        6: begin a=1; b=64; shift=0; end
      endcase
      constant64(pc,1,a); constant64(pc,2,b); drain();
      for(int operation=0;operation<40;operation++) begin
        send(pc,b_insn(operation,3,1,2,shift),b_insn((operation+1)%40,4,1,2,shift)); pc+=8;
        send(pc,imm(5,3,1),regop(6,4,3,4)); pc+=8;
      end
      drain();
      assert(model[0]==0) else $fatal(1,"B changed x0");
    end
    // Unary and shift-immediate encoding bits are not a GPR dependency. The
    // pending divider owns x2 while CPOP (imm[4:0]=2) executes without waiting.
    reset_core();
    begin
      logic [63:0] pc;
      pc='hc000;
      constant64(pc,1,64'h7fffffffffffffff); send(pc,imm(2,0,3),imm(3,0,63)); drain();
    end
    send('hc100,m_insn(2,1,2,4),b_insn(15,4,1,0));
    send('hc108,b_insn(33,5,1,0,2),b_insn(34,6,1,3));
    repeat(12) tick();
    assert(expected.size()==0 && expected_completions.size()==1) else $fatal(1,"B source-use false interlock");
    send('hc110,b_insn(0,7,2,3),b_insn(31,0,1,0)); drain();
    // A deferred return feeding B and a B result feeding M share the ordinary
    // forwarding policy; WAW against a deferred owner still waits for RF write.
    send('hc118,m_insn(8,1,3,0),b_insn(24,9,1,3));
    send('hc120,b_insn(21,10,8,0),m_insn(11,9,3,0));
    send('hc128,b_insn(29,8,10,0),b_insn(38,12,11,3)); drain();

    // Both conditional-zero operations use every bit of rs2. Independent pairs
    // exercise both slots; following consumers check ordinary EX/MEM forwarding.
    for(int scenario=0;scenario<5;scenario++) begin
      logic [63:0] pc;
      logic [63:0] a;
      pc='hd000;
      reset_core();
      case(scenario)
        0: a=0;
        1: a='1;
        2: a=64'h8000000000000000;
        3: a=64'h0123456789abcdef;
        4: a=1;
      endcase
      constant64(pc,1,a); drain();
      for(int condition=0;condition<4;condition++) begin
        logic [63:0] b;
        case(condition)
          0: b=0;
          1: b=1;
          2: b=64'h8000000000000000;
          3: b='1;
        endcase
        constant64(pc,2,b); drain();
        for(int swap=0;swap<2;swap++) begin
          send(pc,regop(3,1,2,swap==0 ? 5 : 7,7),regop(4,1,2,swap==0 ? 7 : 5,7)); pc+=8;
          send(pc,imm(5,3,1),regop(6,4,3)); pc+=8;
        end
        // x0 in either source, discarded writes, and same-group RAW/WAW.
        send(pc,regop(0,1,2,5,7),regop(7,0,2,7,7)); pc+=8;
        send(pc,regop(8,1,0,5,7),regop(9,1,0,7,7)); pc+=8;
        send(pc,regop(8,1,2,7,7),regop(8,1,2,5,7)); pc+=8;
        send(pc,regop(9,1,2,5,7),regop(10,9,2,7,7)); pc+=8;
        drain();
      end
    end
    // Every MOP.R and MOP.RR index in each age slot writes zero, independent
    // of encoded register fields, and forwards that zero to dependent consumers.
    reset_core();
    begin
      logic [63:0] pc;
      pc='he000;
      constant64(pc,1,'1); constant64(pc,2,64'hfedcba9876543210); drain();
      for(int two_sources=0;two_sources<2;two_sources++) begin
        for(int index=0;index<(two_sources!=0 ? 8 : 32);index++) begin
          send(pc,mop_insn(two_sources!=0,index,3,1,2),mop_insn(two_sources!=0,index,4,2,1)); pc+=8;
          send(pc,imm(5,3,1),imm(6,4,2)); pc+=8;
        end
      end
      send(pc,mop_insn(0,31,0,1),mop_insn(1,7,7,0,0)); pc+=8;
      send(pc,imm(8,0,19),mop_insn(1,0,8,8,8)); pc+=8;
      send(pc,imm(9,8,1),regop(10,1,9,7,7)); drain();
    end
    assert(conditional_dual>=40 && mop_dual>=40) else $fatal(1,"conditional/MOP dual issue missing");

    // MOP encoded sources must not wait for either pending deferred register.
    // Conditional-zero must wait for a real rs2 dependency, even when rs1 is x0.
    reset_core();
    begin
      logic [63:0] pc;
      pc='hf000;
      constant64(pc,1,64'h7fffffffffffffff); send(pc,imm(2,0,3),0,1); drain();
    end
    hold_responses=1;
    send('hf100,m_insn(10,1,2,4),imm(30,0,0,3,'h03));
    send('hf108,mop_insn(0,2,3,10),mop_insn(1,7,4,30,10));
    repeat(12) tick();
    assert(expected.size()==0 && expected_completions.size()==2) else $fatal(1,"MOP false source interlock or serialization");
    send('hf110,regop(5,0,30,5,7),imm(6,0,6));
    repeat(12) tick();
    assert(expected.size()==2 && response_count==1) else $fatal(1,"conditional-zero lost rs2 interlock");
    hold_responses=0; drain();
    // Destination WAW still waits, even though neither MOP source is read.
    reset_core(); hold_responses=1;
    send('hf200,imm(30,0,0,3,'h03),imm(1,0,1));
    send('hf208,mop_insn(1,3,30,0,0),imm(2,30,1));
    repeat(12) tick();
    assert(expected.size()==2 && expected_completions.size()==1) else $fatal(1,"MOP lost destination reservation");
    hold_responses=0; drain();

    // Wrong-path results and SYSTEM-opcode lookalikes must not commit. Faults
    // in the younger slot preserve the older MOP/conditional retirement.
    for(int lane=0;lane<2;lane++) begin
      logic [31:0] invalid_word;
      reset_core();
      invalid_word=mop_insn(lane!=0,0,3,1,2)^32'h20000000;
      stop_at(64'('hf300+lane*4),64'('hf300+lane*4),1,2,64'(invalid_word));
      if(lane==0) send('hf300,invalid_word,regop(4,0,0,7,7),2,0,0);
      else send('hf300,mop_insn(0,0,4,1),invalid_word,2,1,0);
      drain();
      stop_at('hf400,'hf440);
      send('hf400,jump(0,64),mop_insn(1,7,5,1,2),2,1,0);
      send('hf408,regop(6,1,0,7,7),mop_insn(0,0,7,1),2,0,0); drain();
      send('hf440,imm(8,5,1),imm(9,6,1)); drain();
    end
    // A same-destination younger load can hit at WB or reserve a deferred write.
    for(int slow=0;slow<2;slow++) begin
      int before_waw;
      reset_core(); lookup_mode=slow==0 ? 1 : 0; hold_responses=slow!=0;
      send('h10000,imm(1,0,'h300),imm(2,0,3)); drain();
      before_waw=slow==0 ? waw_dual : waw_deferred;
      send('h10008,imm(10,0,91),imm(10,1,0,3,'h03));
      if(slow!=0) begin
        repeat(8) tick();
        assert(expected.size()==0 && response_count==1 && waw_deferred==before_waw+1) else $fatal(1,"younger slow-load WAW pair did not retire/reserve");
      end
      send('h10010,imm(11,10,1),imm(12,0,12));
      if(slow!=0) begin
        repeat(6) begin tick(); assert(issued==0) else $fatal(1,"consumer exposed older value behind younger deferred WAW"); end
        hold_responses=0;
      end
      drain();
      assert((slow==0 ? waw_dual : waw_deferred)==before_waw+1) else $fatal(1,"younger load WAW pair was split");
      send('h10018,imm(13,10,0),0,1); drain();
    end

    // Younger multiply/divide writes also follow the older normal-WB write.
    for(int operation=0;operation<2;operation++) begin
      int before_waw;
      reset_core(); send('h10100,imm(1,0,21),imm(2,0,3)); drain();
      before_waw=waw_deferred;
      send('h10108,imm(10,0,91),m_insn(10,1,2,operation==0 ? 0 : 4));
      send('h10110,imm(11,10,1),imm(12,0,12)); drain();
      assert(waw_deferred==before_waw+1) else $fatal(1,"younger M WAW pair was split");
      send('h10118,imm(13,10,0),0,1); drain();
    end

    // A deferred older writer must finish before a same-destination overwrite.
    for(int service=0;service<3;service++) begin
      int before_waw;
      reset_core(); send('h10140,imm(1,0,'h300),imm(2,0,3)); drain();
      before_waw=waw_dual+waw_deferred;
      hold_responses=service==0;
      send('h10148,service==0 ? imm(10,1,0,3,'h03) : m_insn(10,1,2,service==1 ? 0 : 4),imm(10,0,91));
      if(service==0) begin
        repeat(8) tick();
        assert(expected.size()==1 && response_count==1) else $fatal(1,"older deferred WAW did not retain younger overwrite");
        hold_responses=0;
      end
      drain();
      assert(waw_dual+waw_deferred==before_waw) else $fatal(1,"older deferred WAW pair was not split");
      send('h10150,imm(11,10,0),0,1); drain();
    end

    // A retained misaligned younger load preserves the older write on a fault.
    for(int fault=0;fault<2;fault++) begin
      reset_core(); send('h10180,imm(1,0,'h301),0,1); drain();
      split_fault=fault!=0;
      stop_at('h1018c,'h10190,fault!=0 ? 1 : 3,13,'h304);
      send('h10188,imm(10,0,91),imm(10,1,0,3,'h03),2,1,fault==0); drain();
      split_fault=0;
      send('h10190,imm(11,10,0),0,1); drain();
    end

    // Younger lookup/admission faults and replay preserve the older RF value.
    for(int scenario=0;scenario<5;scenario++) begin
      logic [63:0] pc;
      int disposition, cause, before_commits, before_waw;
      reset_core(); send('h10200,imm(1,0,'h300),imm(2,0,3)); drain();
      pc=64'('h10210+scenario*32);
      lookup_mode=scenario<3 ? scenario+3 : 0;
      inject_memory_fault=scenario==3; fault_address='h300;
      block_requests=scenario==4;
      disposition=scenario==0 || scenario==4 ? 2 : 1;
      cause=scenario==1 ? 13 : 5;
      stop_at(pc+4,pc+4,disposition,64'(cause),'h300);
      before_commits=commits;
      send(pc,imm(10,0,91),imm(10,1,0,3,'h03),2,1,0); drain();
      assert(commits==before_commits+1) else $fatal(1,"younger WAW rejection suppressed older retirement");
      inject_memory_fault=0; block_requests=0;
      send(pc+8,imm(11,10,0),0,1); drain();
      if(disposition==2) begin
        lookup_mode=1;
        send(pc+4,imm(10,1,0,3,'h03),0,1); drain();
        send(pc+12,imm(12,10,0),0,1); drain();
      end
      // An older fault cancels a same-destination younger write as well.
      inject_enable=1; inject_pc=pc+16;
      inject_result='{guest:'0,disposition:2'd1,cause:64'd5,value:64'hdead};
      stop_at(pc+16,pc+16,1,5,'hdead);
      before_waw=waw_dual;
      send(pc+16,imm(10,0,1),imm(10,0,2),2,0,0); drain();
      assert(waw_dual==before_waw) else $fatal(1,"older fault allowed younger WAW retirement");
      inject_enable=0;
      send(pc+24,imm(13,10,0),0,1); drain();
    end
    // Both architectural values survive U/I folding, including signed overflow
    // of a 32-bit folded offset, PC carry/borrow, and RV64 modular wrap.
    for(int pc_case=0;pc_case<3;pc_case++) begin
      for(int upper_case=0;upper_case<5;upper_case++) begin
        for(int lower_case=0;lower_case<5;lower_case++) begin
          for(int destination=0;destination<3;destination++) begin
            logic [63:0] pc;
            logic [19:0] upper;
            int lower, rd, before_pairs, before_dual;
            reset_core();
            case(pc_case)
              0: pc=64'hffe;
              1: pc=64'h80000000fffffff8;
              2: pc=64'hfffffffffffffff8;
            endcase
            case(upper_case)
              0: upper=0;
              1: upper=1;
              2: upper=20'h7ffff;
              3: upper=20'h80000;
              4: upper=20'hfffff;
            endcase
            case(lower_case)
              0: lower=-2048;
              1: lower=-1;
              2: lower=0;
              3: lower=1;
              4: lower=2047;
            endcase
            rd=destination==0 ? 3 : destination==1 ? 4 : 0;
            before_pairs=auipc_addi_pairs; before_dual=dual_commits;
            send(pc,{upper,5'd3,7'h17},imm(rd,3,lower));
            send(pc+8,imm(5,3,0),imm(6,rd,0)); drain();
            assert(auipc_addi_pairs==before_pairs+1 && dual_commits>=before_dual+1)
              else $fatal(1,"AUIPC/ADDI did not pair pc=%h upper=%h lower=%0d rd=%0d",pc,upper,lower,rd);
            send(pc+16,imm(7,3,0),imm(8,rd,0)); drain();
          end
        end
      end
    end
    begin
      int before_pairs;
      reset_core(); before_pairs=auipc_addi_pairs; longest_dual_run=0; dual_run=0;
      for(int i=0;i<16;i++) send('h10800+64'(8*i),{20'(i-8),5'd3,7'h17},imm(3,3,i-8));
      drain();
      assert(auipc_addi_pairs==before_pairs+16 && longest_dual_run>=12)
        else $fatal(1,"AUIPC/ADDI lost sustained dual issue");
    end
    // Older/younger destination reservations still block the appropriate prefix;
    // an immediate's apparent rs2 field must not cause a false source hazard.
    for(int reservation=0;reservation<3;reservation++) begin
      int before_pairs, before_commits, pending_rd;
      reset_core(); hold_responses=1;
      pending_rd=reservation==0 ? 3 : reservation==1 ? 4 : 2;
      send('h10900,imm(pending_rd,0,'h300,3,'h03),0,1); repeat(8) tick();
      before_pairs=auipc_addi_pairs; before_commits=commits;
      send('h10904,{20'd1,5'd3,7'h17},imm(4,3,2));
      repeat(6) tick();
      assert(commits==before_commits+(reservation==0 ? 0 : reservation==1 ? 1 : 2))
        else $fatal(1,"AUIPC/ADDI reservation boundary failed kind=%0d",reservation);
      hold_responses=0; drain();
      assert(auipc_addi_pairs==before_pairs+(reservation==1 ? 0 : 1))
        else $fatal(1,"AUIPC/ADDI reservation changed pair ownership kind=%0d",reservation);
      send('h1090c,imm(5,3,0),imm(6,4,0)); drain();
    end
    // Nearby operations retain normal RAW splitting, and x0 is not a producer.
    for(int scenario=0;scenario<4;scenario++) begin
      logic [31:0] first, second;
      int before_pairs, before_dual;
      reset_core(); before_pairs=auipc_addi_pairs; before_dual=dual_commits;
      first={20'd1,5'd3,scenario==2 ? 7'h37 : 7'h17};
      second=imm(4,3,7,scenario==0 ? 4 : 0,scenario==1 ? 'h1b : 'h13);
      if(scenario==3) begin first={20'd1,5'd0,7'h17}; second=imm(4,0,7); end
      send('h10920,first,second); drain();
      assert(auipc_addi_pairs==before_pairs && dual_commits==before_dual+(scenario==3 ? 1 : 0))
        else $fatal(1,"AUIPC/ADDI recognition escaped its boundary kind=%0d",scenario);
      send('h10928,imm(5,3,0),imm(6,4,0)); drain();
    end
    // A stop in either member preserves only the successful prefix, including
    // the intermediate AUIPC value when the rejected ADDI targets the same RF entry.
    for(int lane=0;lane<2;lane++) begin
      for(int replay=0;replay<2;replay++) begin
        int before_pairs;
        reset_core(); before_pairs=auipc_addi_pairs;
        inject_enable=1; inject_pc='h10944+64'(4*lane);
        inject_result='{guest:'0,disposition:2'(replay!=0 ? 2 : 1),cause:64'd5,value:64'hdead};
        stop_at(inject_pc,inject_pc,replay!=0 ? 2 : 1,5,'hdead);
        send('h10944,{20'h80000,5'd3,7'h17},imm(3,3,-2048),2,lane==1,0); drain();
        assert(auipc_addi_pairs==before_pairs+1) else $fatal(1,"qualified AUIPC/ADDI pair split before MEM");
        inject_enable=0;
        send('h1094c,imm(5,3,0),0,1); drain();
        if(replay!=0) begin
          if(lane==0) send('h10944,{20'h80000,5'd3,7'h17},imm(3,3,-2048));
          else send('h10948,imm(3,3,-2048),0,1);
          drain(); send('h10954,imm(6,3,0),0,1); drain();
        end
      end
    end
    reset_core();
    send('h10960,{20'd1,5'd3,7'h17},imm(3,3,1),2,0,0);
    reset_core();
    send('h10968,imm(5,3,0),0,1); drain();

    // All ordinary load widths can use the current older ALU result, not stale RF.
    reset_core(); lookup_mode=1;
    send('h11000,imm(1,0,'h100),imm(2,0,'h200)); drain();
    for(int same_rd=0;same_rd<2;same_rd++) begin
      for(int width=0;width<7;width++) begin
        int before_pairs;
        send('h11008,imm(3,0,'h280),0,1); drain();
        before_pairs=address_pairs;
        send('h11010,regop(3,1,2),imm(same_rd!=0 ? 3 : 4,3,0,width,'h03));
        send('h11018,imm(5,same_rd!=0 ? 3 : 4,1),0,1); drain();
        assert(address_pairs==before_pairs+1) else $fatal(1,"zero-offset load address pair split width=%0d same_rd=%0d",width,same_rd);
        send('h11020,imm(6,3,0),imm(7,same_rd!=0 ? 3 : 4,0)); drain();
      end
    end
    // Admission fixes the bypass bit even when the pair reads an older EX result.
    begin
      int before_pairs;
      before_pairs=address_pairs;
      send('h11028,imm(3,0,'h288),0,1);
      send('h1102c,imm(3,0,'h308),imm(4,3,0,3,'h03));
      send('h11034,imm(5,4,0),0,1); drain();
      assert(address_pairs==before_pairs+1) else $fatal(1,"forwarded stale base defeated paired address");
      // AUIPC uses its PC result; the load can overwrite that same register.
      before_pairs=address_pairs;
      send('h320,{20'd0,5'd3,7'h17},imm(3,3,0,3,'h03)); drain();
      assert(address_pairs==before_pairs+1) else $fatal(1,"AUIPC zero-offset load did not pair");
      send('h328,imm(5,3,0),0,1); drain();
      // Word and shift producers use their selected ALU result, not just the adder.
      before_pairs=address_pairs;
      send('h11038,imm(3,1,1,1),imm(4,3,0,3,'h03)); drain();
      send('h11040,imm(3,1,'h200,0,'h1b),imm(0,3,0,3,'h03)); drain();
      assert(address_pairs==before_pairs+2) else $fatal(1,"selected ALU result/x0 load did not pair");
      send('h11048,imm(5,3,0),imm(6,0,0)); drain();
      // An x0 "producer" must not redirect a load away from architectural zero.
      before_pairs=address_pairs;
      send('h11050,imm(0,0,'h300),imm(4,0,0,3,'h03)); drain();
      assert(address_pairs==before_pairs) else $fatal(1,"x0 was treated as a paired address producer");
      dual_run=0; longest_dual_run=0; before_pairs=address_pairs;
      for(int i=0;i<16;i++) send(64'('h11060+8*i),imm(3,0,'h300+8*i),imm(10+i,3,0,3,'h03));
      drain();
      assert(address_pairs==before_pairs+16 && longest_dual_run>=12) else $fatal(1,"paired loads lost sustained dual issue");
    end

    // A paired miss reserves the younger value and blocks consumers until return.
    for(int same_rd=0;same_rd<2;same_rd++) begin
      int before_pairs;
      reset_core(); lookup_mode=0; hold_responses=1;
      before_pairs=address_pairs;
      send('h11100,imm(3,0,'h300),imm(same_rd!=0 ? 3 : 4,3,0,3,'h03));
      repeat(8) tick();
      assert(address_pairs==before_pairs+1 && expected.size()==0 && response_count==1) else $fatal(1,"paired address miss did not authorize both owners");
      send('h11108,imm(5,same_rd!=0 ? 3 : 4,0),0,1);
      repeat(6) begin tick(); assert(issued==0) else $fatal(1,"paired miss consumer used stale data"); end
      hold_responses=0; drain();
      send('h1110c,imm(6,3,0),imm(7,same_rd!=0 ? 3 : 4,0)); drain();
    end
    // Waiving the load's base read does not waive the producer's older WAW hazard.
    reset_core(); hold_responses=1;
    send('h11120,imm(3,0,0,3,'h03),0,1); repeat(8) tick();
    begin
      int before_pairs;
      before_pairs=address_pairs;
      lookup_mode=1;
      send('h11124,imm(3,0,'h300),imm(4,3,0,3,'h03));
      repeat(6) begin tick(); assert(issued==0) else $fatal(1,"address bypass waived producer WAW interlock"); end
      hold_responses=0; drain();
      assert(address_pairs==before_pairs+1) else $fatal(1,"address pair was not retained through producer WAW wait");
      send('h1112c,imm(5,4,0),0,1); drain();
    end

    // Offset and resource boundaries retain ordinary dependent instruction ordering.
    for(int scenario=0;scenario<6;scenario++) begin
      int before_pairs;
      logic [31:0] first, second;
      reset_core(); lookup_mode=scenario==5 ? 0 : 1;
      send('h11200,imm(1,0,'h180),imm(2,0,2)); drain();
      case(scenario)
        0: begin first=imm(3,0,'h300); second=imm(4,3,8,3,'h03); end
        1: begin first=imm(3,0,'h308); second=imm(4,3,-8,3,'h03); end
        2: begin first=m_insn(3,1,2,0); second=imm(4,3,0,3,'h03); end
        3: begin first=m_insn(3,1,2,4); second=imm(4,3,0,3,'h03); end
        4: begin first=imm(3,0,'h300); second=store(2,3,0,3); end
        5: begin first=imm(3,0,'h300); second=atomic_insn(2,3,4,3); end
      endcase
      before_pairs=address_pairs;
      send('h11208,first,second); drain();
      assert(address_pairs==before_pairs) else $fatal(1,"ineligible producer/load pair bypassed ordering scenario=%0d",scenario);
      send('h11210,imm(5,3,0),imm(6,4,0)); drain();
    end

    // Fault/replay reports the bypassed address, while the producer remains committed.
    for(int scenario=0;scenario<5;scenario++) begin
      int before_pairs;
      reset_core(); lookup_mode=scenario<3 ? scenario+3 : 0;
      inject_memory_fault=scenario==3; fault_address='h300; block_requests=scenario==4;
      stop_at('h11304,'h11304,scenario==0 || scenario==4 ? 2 : 1,scenario==1 ? 13 : 5,'h300);
      before_pairs=address_pairs;
      send('h11300,imm(3,0,'h300),imm(3,3,0,3,'h03),2,1,0); drain();
      assert(address_pairs==before_pairs+1) else $fatal(1,"fault/replay address pair split");
      inject_memory_fault=0; block_requests=0;
      send('h11308,imm(4,3,0),0,1); drain();
      if(scenario==0 || scenario==4) begin
        lookup_mode=1; send('h11304,imm(3,3,0,3,'h03),0,1); drain();
        send('h1130c,imm(4,3,0),0,1); drain();
      end
    end
    // Misaligned zero-offset loads retain the bypassed address through the split owner.
    for(int fault=0;fault<2;fault++) begin
      int before_pairs;
      reset_core(); split_fault=fault!=0;
      before_pairs=address_pairs;
      stop_at('h11404,'h11408,fault!=0 ? 1 : 3,13,'h304);
      send('h11400,imm(3,0,'h301),imm(3,3,0,3,'h03),2,1,fault==0); drain();
      assert(address_pairs==before_pairs+1) else $fatal(1,"misaligned zero-offset address pair split");
      split_fault=0; send('h11408,imm(4,3,0),0,1); drain();
    end
    // An older fault may perform a speculative lookup, never younger authorization.
    reset_core(); inject_enable=1; inject_pc='h11500;
    inject_result='{guest:'0,disposition:2'd1,cause:64'd5,value:64'hdead};
    stop_at('h11500,'h11500,1,5,'hdead);
    send('h11500,imm(3,0,'h300),imm(3,3,0,3,'h03),2,0,0); drain();
    inject_enable=0; send('h11508,imm(4,3,0),0,1); drain();
    // Reset cancels a pair already admitted to EX, including its bypass selection.
    send('h11510,imm(3,0,'h300),imm(3,3,0,3,'h03),2,0,0); tick();
    reset_core(); drain();
    send('h11518,imm(4,3,0),0,1); drain();
    // Every store width/lane consumes the selected ALU result, not the stale rs2.
    for(int slow=0;slow<2;slow++) begin
      reset_core(); lookup_mode=slow==0 ? 1 : 0;
      send('h12000,imm(1,0,'h300),imm(2,0,-128)); drain();
      send('h12008,imm(4,0,65),0,1); drain();
      for(int width=0;width<4;width++) begin
        for(int lane=0;lane<8;lane+=1<<width) begin
          int before_pairs;
          before_pairs=store_data_pairs;
          send('h12010,imm(3,0,17),0,1);
          send('h12014,width%2==0 ? regop(3,2,4) : imm(3,2,3,1,'h1b),store(3,1,(slow==0 ? 24 : -8)+lane,width)); drain();
          assert(store_data_pairs==before_pairs+1) else $fatal(1,"store-data pair split slow=%0d width=%0d lane=%0d",slow,width,lane);
          send('h1201c,imm(5,1,(slow==0 ? 24 : -8)+lane,width,'h03),imm(6,3,0)); drain();
        end
      end
    end
    // A stream keeps both lanes busy; a nonzero negative offset needs no extra adder.
    begin
      int before_pairs;
      reset_core(); lookup_mode=1;
      send('h12040,imm(1,0,'h300),0,1); drain();
      before_pairs=store_data_pairs; dual_run=0; longest_dual_run=0;
      for(int i=0;i<16;i++) send(64'('h12048+8*i),imm(3,0,i-8),store(3,1,-8,3));
      drain();
      assert(store_data_pairs==before_pairs+16 && longest_dual_run>=12) else $fatal(1,"store-data bypass lost sustained dual issue");
    end
    // Producer WAW admission remains mandatory even though its result replaces rs2.
    begin
      int before_pairs;
      reset_core(); hold_responses=1;
      send('h12100,imm(1,0,'h300),0,1); drain();
      send('h12104,imm(3,1,0,3,'h03),0,1); repeat(8) tick();
      lookup_mode=1; before_pairs=store_data_pairs;
      send('h12108,imm(3,0,-93),store(3,1,8,3));
      repeat(6) begin tick(); assert(issued==0) else $fatal(1,"store bypass waived producer WAW"); end
      hold_responses=0; drain();
      assert(store_data_pairs==before_pairs+1) else $fatal(1,"store pair not retained through producer wait");
    end
    // A dependent address and deferred producers still split; x0 supplies zero.
    for(int scenario=0;scenario<4;scenario++) begin
      int before_pairs;
      logic [31:0] first, second;
      reset_core(); lookup_mode=1;
      send('h12140,imm(1,0,'h300),imm(2,0,2)); drain();
      case(scenario)
        0: begin first=imm(3,0,'h308); second=store(3,3,0,3); end
        1: begin first=m_insn(3,1,2,0); second=store(3,1,0,3); end
        2: begin first=imm(3,1,0,3,'h03); second=store(3,1,8,3); end
        3: begin first=imm(0,0,99); second=store(0,1,0,3); end
      endcase
      before_pairs=store_data_pairs;
      send('h12148,first,second); drain();
      assert(store_data_pairs==before_pairs) else $fatal(1,"ineligible store-data producer paired scenario=%0d",scenario);
      send('h12150,imm(4,scenario==0 ? 3 : 1,scenario==2 ? 8 : 0,3,'h03),0,1); drain();
    end
    // Rejection retires the producer once and publishes no younger store effect.
    for(int scenario=0;scenario<6;scenario++) begin
      int before_pairs, before_stores, before_commits;
      bit replay;
      reset_core();
      send('h12200,imm(1,0,'h300),0,1); drain();
      lookup_mode=scenario<3 ? scenario+3 : scenario==5 ? 1 : 0;
      inject_memory_fault=scenario==3; fault_address='h308;
      block_requests=scenario==4; block_stores=scenario==5;
      replay=scenario==0 || scenario>=4;
      stop_at('h1220c,'h1220c,replay ? 2 : 1,scenario==1 ? 15 : 7,'h308);
      before_pairs=store_data_pairs; before_stores=stores; before_commits=commits;
      send('h12208,imm(3,0,-111),store(3,1,8,3),2,1,0); drain();
      assert(store_data_pairs==before_pairs+1 && stores==before_stores && commits==before_commits+1)
        else $fatal(1,"rejected store pair lost prefix or caused side effect scenario=%0d",scenario);
      inject_memory_fault=0; block_requests=0; block_stores=0;
      send('h12210,imm(4,3,0),0,1); drain();
      if(replay) begin
        lookup_mode=1; send('h1220c,store(3,1,8,3),0,1); drain();
        send('h12214,imm(5,1,8,3,'h03),0,1); drain();
      end
    end
    // Split service retains raw bypassed data, including bytes across beat boundaries.
    for(int width=1;width<4;width++) begin
      int before_pairs, before_splits;
      reset_core();
      send('h12300,imm(1,0,'h300),0,1); drain();
      before_pairs=store_data_pairs; before_splits=split_requests;
      stop_at('h1230c,'h12310,3);
      send('h12308,imm(3,0,-117),store(3,1,7,width)); drain();
      assert(store_data_pairs==before_pairs+1 && split_requests==before_splits+1) else $fatal(1,"split store lost paired owner");
      lookup_mode=1;
      for(int b=0;b<(1<<width);b++) begin send(64'('h12310+4*b),imm(4,1,7+b,4,'h03),0,1); drain(); end
    end
    // Older failure and reset cancel speculative store data without a memory effect.
    reset_core(); lookup_mode=1;
    send('h12400,imm(1,0,'h300),0,1); drain();
    begin
      int before_stores;
      before_stores=stores;
      inject_enable=1; inject_pc='h12408;
      inject_result='{guest:'0,disposition:2'd1,cause:64'd5,value:64'hdead};
      stop_at('h12408,'h12408,1,5,'hdead);
      send('h12408,imm(3,0,77),store(3,1,0,3),2,0,0); drain();
      assert(stores==before_stores) else $fatal(1,"older fault authorized younger store");
      inject_enable=0;
      send('h12410,imm(3,0,88),store(3,1,0,3),2,0,0); tick();
      reset_core(); drain();
      assert(stores==before_stores) else $fatal(1,"reset leaked paired store");
    end
    // NTL applies to the next instruction, including a younger co-retiring
    // memory operation. An ordinary target consumes it; hints replace it.
    for(int locality=1;locality<=4;locality++) begin
      logic [31:0] ntl;
      int before_dual;
      ntl=32'h00000033 | (32'(locality+1)<<20);
      reset_core(); before_dual=dual_commits;
      expect_instruction('h13000,ntl); expect_instruction('h13004,imm(3,0,'h300,3,'h03));
      expected_requests[$].locality=3'(locality);
      send('h13000,ntl,imm(3,0,'h300,3,'h03),2,0,0); drain();
      assert(dual_commits==before_dual+1) else $fatal(1,"NTL serialized its younger target");
      send('h13008,imm(4,0,'h300,3,'h03),0,1); drain();
      send('h1300c,imm(5,0,17),ntl); drain();
      expect_instruction('h13014,store(5,0,'h308,3)); expected_requests[$].locality=3'(locality);
      send('h13014,store(5,0,'h308,3),0,1,0,0); drain();
      send('h13018,ntl,imm(6,0,1)); drain();
      send('h13020,imm(7,0,'h300,3,'h03),0,1); drain();
    end
    reset_core();
    send('h13100,32'h00200033,32'h00500033); drain();
    expect_instruction('h13108,imm(3,0,'h300,3,'h03)); expected_requests[$].locality=4;
    send('h13108,imm(3,0,'h300,3,'h03),0,1,0,0); drain();
    // The encoded selector must not wait on its apparent rs2 register.
    begin
      int before_commits;
      reset_core(); hold_responses=1;
      send('h13180,imm(2,0,'h300,3,'h03),0,1); repeat(10) tick();
      before_commits=commits;
      send('h13184,32'h00200033,imm(3,0,1)); repeat(10) tick();
      assert(commits==before_commits+2 && response_count==1) else $fatal(1,"NTL read its encoded selector or drained memory");
      hold_responses=0; drain();
    end
    // Rejecting the younger target preserves the older retired hint across replay.
    reset_core(); block_requests=1;
    stop_at('h13204,'h13204,2);
    send('h13200,32'h00400033,imm(3,0,'h300,3,'h03),2,1,0); drain();
    block_requests=0;
    expect_instruction('h13204,imm(3,0,'h300,3,'h03)); expected_requests[$].locality=3;
    send('h13204,imm(3,0,'h300,3,'h03),0,1,0,0); drain();
    // A split owner retains its selector until the single completion retires.
    reset_core(); expected_split_locality=2;
    stop_at('h13304,'h13308,3);
    send('h13300,32'h00300033,imm(3,0,'h307,3,'h03)); drain();
    send('h13308,imm(4,0,'h300,3,'h03),0,1); drain();
    // A trapping target clears the hint before handler memory; a killed hint
    // must not establish pending state either.
    for(int fault_slot=0;fault_slot<2;fault_slot++) begin
      reset_core(); inject_enable=1; inject_pc=64'('h13400+4*fault_slot);
      inject_result='{guest:'0,disposition:2'd1,cause:64'd5,value:64'hdead};
      stop_at(inject_pc,inject_pc,1,5,'hdead);
      if(fault_slot==1) send('h13400,32'h00500033,imm(3,0,1),2,1,0);
      else send('h13400,imm(3,0,1),32'h00500033,2,0,0);
      drain(); inject_enable=0;
      send('h13410,imm(4,0,'h300,3,'h03),0,1); drain();
    end
    // M-mode always owns stimecmp; S-mode requires both STCE and TM, and
    // U-mode cannot access the supervisor CSR even with both gates open.
    for(int scenario=0;scenario<4;scenario++) begin
      logic [63:0] pc;
      pc='h14100;
      reset_core();
      send(pc,imm(1,0,1000),0,1); pc+=4; drain();
      csr_access(pc,1,0,1,'h14d,0); pc+=4;
      csr_access(pc,2,6,0,'h14d,1000); pc+=4;
      if(scenario!=0) begin
        constant64(pc,1,64'h8000000000000000); drain();
        csr_access(pc,1,0,1,'h30a,0); pc+=4;
      end
      csr_access(pc,5,0,scenario==1 ? 0 : 2,'h306,0); pc+=4;
      constant64(pc,1,scenario==3 ? 64'd0 : 64'h800); drain();
      csr_access(pc,1,0,1,'h300,64'ha00000000); pc+=4;
      send(pc,imm(2,0,'h500),0,1); pc+=4; drain();
      csr_access(pc,1,0,2,'h341,0); pc+=4;
      expect_system(pc,32'h30200073); stop_at(pc,'h500,3);
      send(pc,32'h30200073,0,1,0,0); drain();
      if(scenario==2) csr_access('h500,2,6,0,'h14d,1000);
      else begin
        stop_at('h500,0,1,2,64'(csr(2,6,0,'h14d)));
        send('h500,csr(2,6,0,'h14d),0,1,0,0); drain();
        csr_access(0,2,7,0,'h343,64'(csr(2,6,0,'h14d)));
      end
    end
    // A 64-bit comparison, read-only STIP, reprogramming, and locally enabled
    // WFI wake with global SIE clear. No external interrupt pin is asserted.
    begin
      logic [63:0] pc;
      pc='h510;
      reset_core(); supervisor_timer(0);
      csr_access('h500,2,6,0,'h144,0);
      time_counter=timer_compare;
      csr_access('h504,2,6,0,'h144,32);
      csr_access('h508,1,0,0,'h144,32);
      csr_access('h50c,2,6,0,'h144,32);
      constant64(pc,1,timer_compare+1); drain();
      csr_access(pc,1,0,1,'h14d,timer_compare); pc+=4;
      csr_access(pc,2,6,0,'h144,0); pc+=4;
      expect_system(pc,32'h10500073); send(pc,32'h10500073,0,1,0,0); drain();
      assert(sleeping) else $fatal(1,"Sstc WFI did not sleep before deadline");
      stop_at(pc+4,pc+4,3); time_counter=timer_compare+1; drain();
      assert(!sleeping) else $fatal(1,"Sstc did not wake WFI with SIE clear");
    end
    // Delivery stops before a live pair, follows an already retired pair,
    // drains an accepted load, and wakes WFI into a delegated S-mode handler.
    for(int scenario=0;scenario<4;scenario++) begin
      logic [63:0] boundary;
      reset_core(); supervisor_timer(1);
      boundary=scenario==0 ? 'h500 : scenario==3 ? 'h504 : 'h508;
      if(scenario==0) begin
        send('h500,imm(8,0,8),imm(9,0,9),2,0,0);
        wait(memory_stage[0].valid); @(negedge clock);
      end else if(scenario==3) begin
        expect_system('h500,32'h10500073); send('h500,32'h10500073,0,1,0,0); drain();
        assert(sleeping) else $fatal(1,"Sstc enabled WFI did not sleep");
      end else begin
        hold_responses=scenario==2;
        send('h500,scenario==2 ? imm(8,0,'h300,3,'h03) : imm(8,0,8),imm(9,0,9));
        repeat(10) tick();
      end
      stop_at(boundary,'h700,3); time_counter=timer_compare;
      if(scenario==2) begin
        repeat(10) tick();
        assert(response_count==1 && expected_redirects.size()==1) else $fatal(1,"Sstc entered before deferred writeback drain");
        hold_responses=0;
      end
      drain();
      csr_access('h700,2,10,0,'h141,boundary);
      csr_access('h704,2,11,0,'h142,64'h8000000000000005);
      csr_access('h708,2,12,0,'h143,0);
      assert(!sleeping && interrupts==0) else $fatal(1,"Sstc wake or interrupt source");
      if(scenario==3) begin
        logic [63:0] pc;
        pc='h70c;
        constant64(pc,1,timer_compare+10); drain();
        csr_access(pc,1,0,1,'h14d,timer_compare); pc+=4;
        expect_system(pc,32'h10200073); stop_at(pc,boundary,3);
        send(pc,32'h10200073,0,1,0,0); drain();
        send(boundary,imm(13,0,13),0,1); drain();
      end
    end
    // Every conditional predicate uses the new value on rs1, rs2, or both,
    // including signed/unsigned disagreement and both prediction directions.
    for(int f3=0;f3<8;f3++) if(f3!=2 && f3!=3) begin
      for(int dependency=0;dependency<3;dependency++) begin
        for(int value_case=0;value_case<3;value_case++) begin
          for(int predicted=0;predicted<2;predicted++) begin
            logic [63:0] produced, left, right, pc;
            bit taken;
            int before_pairs;
            prediction_t prediction;
            reset_core();
            produced=value_case==0 ? '1 : value_case==1 ? 1 : 0;
            send('h12800,imm(1,0,int'(produced)-1),imm(2,0,1));
            send('h12808,imm(3,0,42),0,1); drain();
            left=dependency==1 ? 1 : produced;
            right=dependency==0 ? 1 : produced;
            case(f3)
              0: taken=left==right;
              1: taken=left!=right;
              4: taken=$signed(left)<$signed(right);
              5: taken=$signed(left)>=$signed(right);
              6: taken=left<right;
              7: taken=left>=right;
            endcase
            pc='h12810;
            prediction='{1'(predicted),pc+4,pc+36,1'b0,2'd0};
            if(taken!=bit'(predicted)) stop_at(pc+4,taken ? pc+36 : pc+8);
            before_pairs=branch_pairs;
            expected_branch_taken[pc+4]=taken;
            send(pc,imm(3,1,1),branch(dependency==1 ? 2 : 3,dependency==0 ? 2 : 3,32,f3),2,1,1,-1,prediction);
            if(taken!=bit'(predicted)) send(pc+8,imm(4,0,99),imm(5,0,99),2,0,0);
            drain();
            assert(branch_pairs==before_pairs+1 && !expected_branch_taken.exists(pc+4))
              else $fatal(1,"dependent conditional branch did not pair/train f3=%0d operand=%0d value=%0d prediction=%0d",f3,dependency,value_case,predicted);
            send(taken ? pc+36 : pc+8,imm(4,3,0),0,1); drain();
          end
        end
      end
    end
    // Registered ALU result selection includes word sign extension, bit
    // manipulation, and PC-based results, not simply an rs1 passthrough.
    for(int producer=0;producer<3;producer++) begin
      logic [31:0] word;
      int before_pairs;
      reset_core();
      send('h12880,imm(1,0,-128),imm(2,0,1)); drain();
      case(producer)
        0: word=imm(3,1,31,1,'h1b);
        1: word=b_insn(29,3,1,0);
        2: word=32'h00000197; // AUIPC x3,0
      endcase
      before_pairs=branch_pairs; stop_at('h1288c,'h128ac);
      send('h12888,word,branch(3,3,32)); drain();
      assert(branch_pairs==before_pairs+1) else $fatal(1,"selected ALU producer branch pair split kind=%0d",producer);
      send('h128ac,imm(4,3,0),0,1); drain();
    end
    // Sixteen dependent not-taken pairs sustain dual retirement.
    begin
      int before_pairs;
      reset_core(); send('h12900,imm(2,0,100),0,1); drain();
      before_pairs=branch_pairs; dual_run=0; longest_dual_run=0;
      for(int i=0;i<16;i++) send('h12908+64'(8*i),imm(3,0,i),branch(3,2,32));
      drain();
      assert(branch_pairs==before_pairs+16 && longest_dual_run>=12)
        else $fatal(1,"dependent branches lost sustained dual issue");
    end
    // The producer's RAW/WAW interlocks still hold both instructions, even
    // when both comparison operands will be replaced after the old load returns.
    for(int destination_wait=0;destination_wait<2;destination_wait++) begin
      int before_pairs;
      reset_core(); hold_responses=1;
      send('h12a00,imm(5,0,'h300),0,1); drain();
      send('h12a04,imm(destination_wait!=0 ? 3 : 1,5,0,3,'h03),0,1); repeat(8) tick();
      before_pairs=branch_pairs; stop_at('h12a0c,'h12a2c);
      expected_branch_taken['h12a0c]=1;
      send('h12a08,imm(3,destination_wait!=0 ? 0 : 1,1),branch(3,3,32));
      repeat(6) begin tick(); assert(issued==0) else $fatal(1,"branch bypass waived producer RAW/WAW"); end
      hold_responses=0; drain();
      // RAW can forward at completion arbitration, whose RF return reserves
      // the younger slot; WAW waits until the write edge and then pairs.
      assert(branch_pairs<=before_pairs+1 && (destination_wait==0 || branch_pairs==before_pairs+1) && !expected_branch_taken.exists('h12a0c))
        else $fatal(1,"branch lost producer wait/return ordering kind=%0d",destination_wait);
    end
    // An unrelated pending comparison operand is never waived.
    begin
      int before_pairs;
      reset_core(); hold_responses=1;
      send('h12a40,imm(1,0,'h300),0,1); drain();
      send('h12a44,imm(2,1,0,3,'h03),0,1); repeat(8) tick();
      before_pairs=branch_pairs;
      send('h12a48,imm(3,0,1),branch(3,2,32));
      repeat(6) begin tick(); assert(issued==0) else $fatal(1,"branch bypass waived unrelated operand"); end
      hold_responses=0; drain();
      assert(branch_pairs==before_pairs) else $fatal(1,"unavailable nonreplaced operand paired");
    end
    // x0 never selects a producer result, even when the older instruction names it.
    begin
      int before_pairs;
      reset_core(); send('h12ac0,imm(2,0,1),0,1); drain();
      before_pairs=branch_pairs; stop_at('h12acc,'h12aec);
      expected_branch_taken['h12acc]=1;
      send('h12ac8,imm(0,0,99),branch(0,2,32,1)); drain();
      assert(branch_pairs==before_pairs && !expected_branch_taken.exists('h12acc))
        else $fatal(1,"x0 became a branch bypass producer");
    end
    // Deferred producers and a dependent JALR retain ordinary splitting.
    for(int producer=0;producer<4;producer++) begin
      logic [31:0] first, second;
      int before_pairs;
      reset_core(); lookup_mode=1;
      send('h12b00,imm(1,0,'h300),imm(2,0,2)); drain();
      case(producer)
        0: first=imm(3,1,0,3,'h03);
        1: first=m_insn(3,1,2,0);
        2: first=m_insn(3,1,2,4);
        3: first=imm(3,0,'h600);
      endcase
      second=producer==3 ? imm(4,3,0,0,'h67) : branch(3,3,32);
      stop_at('h12b0c,producer==3 ? 'h600 : 'h12b2c);
      before_pairs=branch_pairs;
      send('h12b08,first,second); drain();
      assert(branch_pairs==before_pairs) else $fatal(1,"ineligible branch producer paired kind=%0d",producer);
      send('h12b30,imm(5,3,0),0,1); drain();
    end
    // Older MEM fault/replay suppresses even a dependent taken branch's
    // redirect and training; younger rejection preserves the producer once.
    for(int rejected_lane=0;rejected_lane<2;rejected_lane++) begin
      for(int replay=0;replay<2;replay++) begin
        int before_pairs, before_training, before_commits;
        reset_core(); inject_enable=1; inject_pc='h12c00+64'(4*rejected_lane);
        inject_result='{guest:'0,disposition:replay!=0 ? 2'd2 : 2'd1,cause:64'd5,value:64'hdead};
        stop_at(inject_pc,inject_pc,replay!=0 ? 2 : 1,5,'hdead);
        before_pairs=branch_pairs; before_training=branch_updates; before_commits=commits;
        send('h12c00,imm(3,0,1),branch(3,3,32),2,rejected_lane==1,0); drain();
        assert(branch_pairs==before_pairs+1 && branch_updates==before_training && commits==before_commits+rejected_lane)
          else $fatal(1,"rejected branch pair lost age priority lane=%0d replay=%0d",rejected_lane,replay);
        inject_enable=0;
        send('h12c08,imm(4,3,0),0,1); drain();
      end
    end
    // Coordinated reset cancels a pair captured into EX before MEM comparison.
    reset_core();
    send('h12c40,imm(3,0,1),branch(3,3,32),2,0,0); tick();
    reset_core(); drain();
    send('h12c48,imm(4,3,0),0,1); drain();
    // Both comparators resolve independently, including two correctly taken branches.
    for(int outcomes=0;outcomes<4;outcomes++) begin
      int before_dual, before_training;
      logic [63:0] pc;
      prediction_t first_prediction, second_prediction;
      reset_core(); pc='h13000+64'(outcomes*8);
      first_prediction='{1'(outcomes&1),pc,pc+4,1'b0,2'd0};
      second_prediction='{1'((outcomes>>1)&1),pc+4,pc+8,1'b0,2'd0};
      expected_branch_taken[pc]=1'(outcomes&1); expected_branch_taken[pc+4]=1'((outcomes>>1)&1);
      before_dual=dual_branches; before_training=branch_updates;
      send(pc,branch(0,0,4,outcomes[0]?0:1),branch(0,0,4,outcomes[1]?0:1),2,1,1,-1,second_prediction,first_prediction); drain();
      assert(dual_branches==before_dual+1 && branch_updates==before_training+2 && expected_branch_taken.num()==0)
        else $fatal(1,"dual branch outcomes split or training lost outcomes=%0d",outcomes);
    end
    // A sustained two-branch stream must backpressure RR, never lose a table write.
    begin
      int before_dual, before_training;
      reset_core(); before_dual=dual_branches; before_training=branch_updates;
      for(int pair=0;pair<64;pair++) send('h14000+64'(pair*8),branch(0,0,32,1),branch(0,0,32,1));
      drain();
      assert(branch_updates==before_training+128 && dual_branches>=before_dual+2 && max_training_pending==3)
        else $fatal(1,"branch training FIFO pressure: updates=%0d/128 dual=%0d peak=%0d/3",branch_updates-before_training,dual_branches-before_dual,max_training_pending);
    end
    // Oldest correction wins; a younger correction retains both retirement updates.
    for(int correcting_lane=0;correcting_lane<2;correcting_lane++) begin
      int before_training;
      reset_core(); before_training=branch_updates;
      stop_at('h15000+64'(correcting_lane*4),'h15020+64'(correcting_lane*4));
      send('h15000,branch(0,0,32,correcting_lane==0?0:1),branch(0,0,32,correcting_lane==1?0:1),2,1,correcting_lane==1); drain();
      assert(branch_updates==before_training+1+correcting_lane) else $fatal(1,"dual branch recovery trained wrong prefix");
    end
    // Fault/replay in either branch qualifies only its successful older prefix.
    for(int rejected_lane=0;rejected_lane<2;rejected_lane++) for(int replay=0;replay<2;replay++) begin
      int before_training, before_commits;
      reset_core(); before_training=branch_updates; before_commits=commits;
      inject_enable=1; inject_pc='h15100+64'(rejected_lane*4);
      inject_result='{guest:'0,disposition:replay!=0?2'd2:2'd1,cause:64'd5,value:64'hdead};
      stop_at(inject_pc,inject_pc,replay!=0?2:1,5,'hdead);
      send('h15100,branch(0,0,32,1),branch(0,0,32,1),2,rejected_lane==1,0); drain();
      // Trap entry clears table training, including the simultaneous successful prefix.
      assert(branch_updates==before_training+(replay!=0?rejected_lane:0) && commits==before_commits+rejected_lane)
        else $fatal(1,"branch rejection prefix mismatch lane=%0d replay=%0d updates=%0d commits=%0d",rejected_lane,replay,branch_updates-before_training,commits-before_commits);
    end
    // Two calls split, but a conditional plus one call may retire together.
    begin
      int before_dual, before_ras;
      prediction_t first_prediction, second_prediction;
      reset_core(); before_dual=dual_branches; before_ras=ras_resolutions;
      first_prediction='{1'b1,64'h15200,64'h15204,1'b0,2'd1};
      second_prediction='{1'b1,64'h15204,64'h15208,1'b0,2'd1};
      send('h15200,jump(1,4),jump(5,4),2,1,1,-1,second_prediction,first_prediction); drain();
      assert(dual_branches==before_dual && ras_resolutions==before_ras+2) else $fatal(1,"two RAS actions issued together");
      reset_core(); before_dual=dual_branches; before_ras=ras_resolutions;
      second_prediction='{1'b1,64'h15244,64'h15248,1'b0,2'd1};
      send('h15240,branch(0,0,32,1),jump(1,4),2,1,1,-1,second_prediction); drain();
      assert(dual_branches==before_dual+1 && ras_resolutions==before_ras+1) else $fatal(1,"one-RAS pair serialized or RAS resolution lost");
    end
    $display("Dual branches: %0d; RAS resolutions: %0d; peak queued training: %0d",dual_branches,ras_resolutions,max_training_pending-1);
    // Ssnpm masks data addresses after generation; neither GPR values nor code PCs are modified.
    for(int pmm=2;pmm<=3;pmm++) begin
      logic [63:0] pc, tag;
      pc='h16000; tag=pmm==2 ? 64'hfe00000000000300 : 64'habcd000000000300;
      reset_core();
      constant64(pc,1,64'(pmm)<<32); drain(); csr_access(pc,1,0,1,'h10a,0); pc+=4;
      constant64(pc,1,tag); drain();
      constant64(pc,2,64'h20000); drain(); csr_access(pc,1,0,2,'h300,64'ha00000000); pc+=4; // MPRV U
      expect_system(pc,imm(3,1,0,3,'h03),64'h9f9e9d9c9b9a9998);
      expect_memory('h300,0,8,0);
      send(pc,imm(3,1,0,3,'h03),0,1,0,0); drain(); pc+=4;
      send(pc,imm(4,1,0),0,1); drain(); // Still holds the complete tagged pointer.
    end
    // A dual-retirement count advances the one implemented HPM counter by two.
    begin
      logic [63:0] pc;
      pc='h17000;
      reset_core(); csr_access(pc,5,0,8,'h320,0); pc+=4;
      csr_access(pc,5,0,2,'h323,0); pc+=4; // instret selector
      csr_access(pc,5,0,0,'hb03,0); pc+=4;
      csr_access(pc,5,0,0,'h320,8); pc+=4;
      send(pc,imm(3,0,1),imm(4,0,2)); pc+=8; drain();
      csr_access(pc,5,0,8,'h320,0); pc+=4;
      csr_access(pc,2,5,0,'hb03,3); // pair plus the inhibiting CSR's retiring edge
    end
    // Overflow is based on the carry from the full two-instruction retirement count.
    begin
      logic [63:0] pc;
      pc='h17800; reset_core();
      csr_access(pc,5,0,8,'h320,0); pc+=4; csr_access(pc,5,0,2,'h323,0); pc+=4;
      constant64(pc,1,64'hffffffffffffffff); drain(); csr_access(pc,1,0,1,'hb03,0); pc+=4;
      csr_access(pc,5,0,0,'h320,8); pc+=4; send(pc,imm(3,0,1),imm(4,0,2)); pc+=8; drain();
      csr_access(pc,5,0,8,'h320,0); pc+=4; csr_access(pc,2,5,0,'hb03,2); pc+=4;
      csr_access(pc,2,6,0,'h344,8192); pc+=4; csr_access(pc,2,7,0,'h323,64'h8000000000000002); pc+=4;
      constant64(pc,1,64'h4000000000000002); drain(); csr_access(pc,1,0,1,'h323,64'h8000000000000002); pc+=4;
      csr_access(pc,5,0,0,'hb03,2); pc+=4; csr_access(pc,5,0,0,'h320,8); pc+=4;
      send(pc,imm(3,0,3),imm(4,0,4)); pc+=8; drain(); csr_access(pc,2,5,0,'hb03,0);
    end
    // State-enable gates lower-mode access to senvcfg, independently of its PMM bits.
    for(int enabled=0;enabled<2;enabled++) begin
      logic [63:0] pc;
      pc='h18000;
      reset_core();
      if(enabled!=0) begin
        constant64(pc,1,64'h4000000000000000); drain(); csr_access(pc,1,0,1,'h30c,0); pc+=4;
      end
      constant64(pc,1,'h800); drain(); csr_access(pc,1,0,1,'h300,64'ha00000000); pc+=4;
      send(pc,imm(2,0,'h500),0,1); pc+=4; drain(); csr_access(pc,1,0,2,'h341,0); pc+=4;
      expect_system(pc,32'h30200073); stop_at(pc,'h500,3); send(pc,32'h30200073,0,1,0,0); drain();
      if(enabled!=0) csr_access('h500,2,3,0,'h10a,0);
      else begin
        stop_at('h500,0,1,2,64'(csr(2,3,0,'h10a))); send('h500,csr(2,3,0,'h10a),0,1,0,0); drain();
      end
    end
    // HLV.D has zero decoded displacement despite nonzero instruction[31:20].
    // It uses the same address route as ordinary loads, without a dependent adder.
    begin
      logic [63:0] pc;
      logic [31:0] hlv;
      int before_pairs;
      pc='h19000; hlv=32'h6c00c1f3; // hlv.d x3,(x1)
      reset_core(); before_pairs=guest_address_pairs;
      expect_instruction(pc,imm(1,0,'h300));
      expect_system(pc+4,hlv,64'h9f9e9d9c9b9a9998); expect_memory('h300,0,8,0);
      send(pc,imm(1,0,'h300),hlv,2,0,0); drain(); pc+=8;
      assert(guest_address_pairs==before_pairs+1) else $fatal(1,"decoded zero-displacement guest load did not pair");
      send(pc,imm(4,3,1),0,1); drain();
      // Full invalidation fences serialize and invalidate once at successful WB.
      for(int i=0;i<4;i++) begin
        logic [31:0] word;
        int before_flush;
        word=i==0 ? 32'h22000073 : i==1 ? 32'h62000073 : i==2 ? 32'h26000073 : 32'h66000073;
        before_flush=translation_invalidations;
        pc+=4; expect_system(pc,word); stop_at(pc,pc+4,3); send(pc,word,0,1,0,0); drain();
        assert(translation_invalidations==before_flush+1) else $fatal(1,"HFENCE/HINVAL invalidation");
      end
    end
    // Enter VS using MPV; an explicit guest load traps as virtual instruction,
    // with the older successful slot preserved and no accepted memory request.
    for(int lane=0;lane<2;lane++) begin
      logic [63:0] pc;
      logic [31:0] hlv;
      int before_commits, before_requests;
      pc='h1a000; hlv=32'h6c00c1f3;
      reset_core(); constant64(pc,1,64'h8000000800); drain(); csr_access(pc,1,0,1,'h300,64'ha00000000); pc+=4;
      send(pc,imm(2,0,'h500),0,1); pc+=4; drain(); csr_access(pc,1,0,2,'h341,0); pc+=4;
      expect_system(pc,32'h30200073); stop_at(pc,'h500,3); send(pc,32'h30200073,0,1,0,0); drain();
      before_commits=commits; before_requests=requests;
      stop_at('h500+64'(lane*4),0,1,22,64'(hlv));
      if(lane==0) send('h500,hlv,imm(5,0,7),2,0,0);
      else send('h500,imm(5,0,7),hlv,2,1,0);
      drain();
      assert(commits==before_commits+lane && requests==before_requests) else $fatal(1,"virtual fault retirement prefix");
      csr_access(0,2,6,0,'h342,22);
    end
    // Either fault slot retains implicit-PTE provenance into the architectural trap CSRs.
    for(int lane=0;lane<2;lane++) begin
      reset_core(); inject_enable=1; inject_pc='h1b000+64'(lane*4);
      inject_result='{disposition:2'd1,cause:64'd21,value:64'h500008,guest:{1'b1,64'h20000,1'b1}};
      stop_at(inject_pc,0,1,21,'h500008);
      if(lane==0) send('h1b000,imm(5,0,7),imm(6,0,8),2,0,0);
      else send('h1b000,imm(5,0,7),imm(6,0,8),2,1,0);
      drain(); inject_enable=0;
      csr_access(0,2,7,0,'h34b,'h8000);
      csr_access(4,2,8,0,'h34a,'h3000);
      csr_access(8,2,9,0,'h343,'h500008);
    end
    $display("Paired load addresses: %0d; paired store data: %0d; paired branch operands: %0d",address_pairs,store_data_pairs,branch_pairs);
    $display("Paired AUIPC/ADDI operations: %0d",auipc_addi_pairs);
    $display("Same-destination writes: %0d normal-WB pairs, %0d younger deferred pairs",waw_dual,waw_deferred);
    $display("Memory: %0d accepted, %0d responses, %0d reset-canceled, %0d stores, max %0d outstanding, %0d overlap retirements, %0d shared-write cycles", requests, responses, canceled, stores, max_outstanding, overlap_retirements, shared_writes);
    assert(branch_updates>0) else $fatal(1,"no retired branch training");
    assert(direction_updates>0 && history_recoveries>0) else $fatal(1,"gshare training/recovery not exercised");
    $display("Zicond/Zimop: %0d conditional pairs, %0d MOP pairs",conditional_dual,mop_dual);
    $display("RV2Wide passed: %0d retirements, %0d dual cycles, %0d stops in %0d cycles", commits, dual_commits, stops, cycles);
    $finish;
  end
endmodule
