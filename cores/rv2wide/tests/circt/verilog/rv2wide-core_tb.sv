// SPDX-License-Identifier: Apache-2.0
module rv2wide_core_tb;
  typedef struct packed { logic [63:0] cause, value; } fetch_fault_t;
  typedef struct packed { logic valid; fetch_fault_t bits; } fetch_fault_flow_t;
  typedef struct packed { logic [63:0] pc; logic [31:0] instruction; fetch_fault_flow_t fault; } instruction_t;
  typedef struct packed { logic [1:0] count; instruction_t [1:0] entries; } packet_t;
  typedef struct packed { logic valid; packet_t bits; } packet_flow_t;
  typedef struct packed { logic [1:0] disposition; logic [63:0] cause, value; } resolution_t;
  typedef struct packed { logic valid; resolution_t bits; } resolution_flow_t;
  typedef struct packed { logic valid; instruction_t bits; } instruction_flow_t;
  typedef struct packed { instruction_t fetched; logic [4:0] rd; logic write; logic [63:0] data; logic deferred; } retirement_t;
  typedef struct packed { logic valid; retirement_t bits; } retirement_flow_t;
  typedef struct packed { logic [63:0] pc, target; resolution_t resolution; } redirect_t;
  typedef struct packed { logic valid; redirect_t bits; } redirect_flow_t;
  typedef struct packed { logic [63:0] address; logic write; logic [1:0] width; logic [63:0] data; logic [7:0] mask; } memory_req_t;
  typedef struct packed { logic valid; memory_req_t bits; } memory_req_flow_t;
  typedef struct packed { logic valid; logic [63:0] bits; } memory_resp_flow_t;
  typedef struct packed { logic request_ready; resolution_flow_t fault; memory_resp_flow_t response; logic drained, ordered_busy; } memory_in_t;
  typedef struct packed { memory_req_flow_t request; logic response_ready; } memory_out_t;
  typedef struct packed { logic [2:0] outcome; logic [63:0] data; } lookup_t;
  typedef struct packed { logic valid; lookup_t bits; } lookup_flow_t;
  typedef struct packed { lookup_flow_t response; logic commit_ready; } pipeline_in_t;
  typedef struct packed { memory_req_flow_t request; logic commit; } pipeline_out_t;

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
  logic sleeping;
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
  int invalidations=0;

  RV2WideCore dut(
    .translation_state(), .translation_flush(), .instruction_invalidate_out(instruction_invalidate),
    .interrupts(interrupts), .hart_id(64'd7), .time_counter(64'd123), .sleeping(sleeping),
    .clock(clock), .reset(reset), .instructions_in(instructions), .instructions_out(ready),
    .resolution_0_in(resolution[0]), .resolution_1_in(resolution[1]),
    .memory_stage_0_out(memory_stage[0]), .memory_stage_1_out(memory_stage[1]),
    .retired_0_out(retired[0]), .retired_1_out(retired[1]),
    .redirect_out(redirect), .fetch_flush_out(fetch_flush), .issued(issued), .retired_count(retired_count), .instruction_capacity(instruction_capacity),
    .memory_in({memory_in.request_ready, memory_in.fault, memory_in.response, memory_in.drained, 1'b0}),
    .memory_out(memory_out), .pipeline_in(pipeline_in), .pipeline_out(pipeline_out), .completed_out(completed)
  );
  always #5 clock = ~clock;
  always @(posedge clock) if(!reset && instruction_invalidate) begin
    invalidations++;
    assert(retired[0].valid && retired[0].bits.fetched.instruction==32'h0000100f && redirect.valid && redirect.bits.resolution.disposition==3)
      else $fatal(1,"invalidation must be a retiring WB FENCE.I event");
  end
  assign memory_in.ordered_busy = 1'b0;
  always_comb begin
    memory_in.fault.valid = memory_out.request.valid && inject_memory_fault && memory_out.request.bits.address == fault_address;
    memory_in.fault.bits = '{disposition: 2'd1, cause: memory_out.request.bits.write ? 64'd7 : 64'd5, value: fault_address};
    memory_in.request_ready = !block_requests && response_count < 16 && !memory_in.fault.valid;
    memory_in.response.valid = response_count > 0 && cycles >= response_due[response_read] && !hold_responses;
    memory_in.response.bits = response_data[response_read];
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

  always @(posedge clock) begin
    if (reset) begin
      response_count <= 0; response_read <= 0; response_write <= 0;
      lookup_response <= '0; store_candidate_valid <= 0;
    end else begin
      logic push_response, pop_response;
      push_response = memory_out.request.valid && memory_in.request_ready;
      pop_response = memory_in.response.valid && memory_out.response_ready;
      response_count <= response_count + int'(push_response) - int'(pop_response);
      if (response_count > max_outstanding) max_outstanding = response_count;
      lookup_response.valid <= pipeline_out.request.valid;
      if (pipeline_out.request.valid) lookups++;
      lookup_request <= pipeline_out.request.bits;
      lookup_response.bits.outcome <= lookup_mode == 1 ? (pipeline_out.request.bits.write ? 3'd2 : 3'd1) : 3'(lookup_mode);
      for (int b = 0; b < 8; b++)
        lookup_response.bits.data[b*8 +: 8] <= memory_bytes[int'(pipeline_out.request.bits.address[11:3])*8 + b];
      store_candidate <= lookup_request;
      store_candidate_valid <= lookup_response.valid && lookup_response.bits.outcome == 2;
      if (memory_out.request.valid && !memory_in.request_ready) begin
        stall_cycles++;
      end
      if (push_response) begin
        check_request(memory_out.request.bits);
        for (int b = 0; b < 8; b++)
          response_data[response_write][b*8 +: 8] <= memory_bytes[int'(memory_out.request.bits.address[11:3])*8 + b];
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
      if (cycles > 10000) $fatal(1, "watchdog");
      assert (issued <= 2 && retired_count <= 2) else $fatal(1, "non-prefix count");
      if (int'(instruction_capacity) < minimum_instruction_capacity) minimum_instruction_capacity = int'(instruction_capacity);
      assert (!retired[1].valid || retired[0].valid) else $fatal(1, "younger retired alone");
      if(memory_stage[0].valid && memory_stage[0].bits.instruction[6:0]==7'h73)
        assert(!memory_stage[1].valid) else $fatal(1,"system instruction did not issue alone");
      if(memory_stage[1].valid)
        assert(memory_stage[1].bits.instruction[6:0]!=7'h73) else $fatal(1,"system instruction in younger slot");
      if (retired[0].valid && retired[1].valid && retired[0].bits.write && retired[1].bits.write)
        assert (retired[0].bits.rd != retired[1].bits.rd) else $fatal(1, "same-group WAW was not split");
      assert (int'(retired_count) == int'(retired[0].valid) + int'(retired[1].valid)) else $fatal(1, "retirement count mismatch");
      if (issued == 1) single_issues++;
      if (response_count > 0 && retired_count != 0) overlap_retirements++;
      if (completed.valid && completed.bits.write) begin
        assert (!retired[1].valid) else $fatal(1, "completion collided with younger slot");
        if (retired[0].valid && retired[0].bits.write && !retired[0].bits.deferred) shared_writes++;
      end
      if (retired_count == 2) begin
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
          assert (retired[lane].bits.fetched == want.fetched && retired[lane].bits.write == want.write)
            else $fatal(1, "retirement order/control pc=%h expected=%h", retired[lane].bits.fetched.pc, want.fetched.pc);
          if (retired[lane].bits.deferred) begin
            expected_completions.push_back(want);
            response_owners.push_back(want);
          end
          if (want.write && !retired[lane].bits.deferred)
            assert (retired[lane].bits.rd == want.rd && retired[lane].bits.data == want.data)
              else $fatal(1, "result pc=%h rd=%d got=%h expected rd=%d data=%h", want.fetched.pc, retired[lane].bits.rd, retired[lane].bits.data, want.rd, want.data);
          commits++;
        end
      end
      if (memory_in.response.valid && memory_out.response_ready) begin
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
        assert (expected_completions.size() > 0) else $fatal(1, "unowned memory completion");
        want = expected_completions.pop_front();
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
    item.write = write_access;
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
    assert (actual.address == want.address && actual.write == want.write && actual.width == want.width && actual.mask == want.mask) else $fatal(1, "request mismatch got=%h expected=%h", actual, want);
    if (want.write) begin
      for (int b = 0; b < 8; b++) begin
        if (want.mask[b]) begin
          assert (actual.data[b*8 +: 8] == want.data[b*8 +: 8]) else $fatal(1, "store data mismatch");
          memory_bytes[int'(actual.address[11:3])*8+b] <= actual.data[b*8 +: 8];
        end
      end
      stores++;
    end
  endtask

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
    case (op)
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
        assert (address < 4096 && (address % 64'(bytes)) == 0) else $fatal(1, "oracle expected unaligned/outside access");
        if (lookup_mode != 1 || op == 'h23) expect_memory(address, op == 'h23, bytes, b);
        if (op == 'h23) begin
          writes = 0;
          for (int i = 0; i < bytes; i++) model_bytes[int'(address) + i] = b[i*8 +: 8];
        end else begin
          for (int i = 0; i < bytes; i++) value[i*8 +: 8] = model_bytes[int'(address) + i];
          if ((f3 & 4) == 0 && bytes < 8 && value[bytes*8-1]) value |= '1 << (bytes*8);
        end
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
        case (f3)
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
    item = '0;
    item.fetched.pc = pc; item.fetched.instruction = word;
    item.rd = word[11:7]; item.write = writes && word[11:7] != 0; item.data = value;
    if (item.write) model[item.rd] = value;
    expected.push_back(item);
  endtask

  task automatic tick;
    @(posedge clock); #1;
  endtask
  task automatic send(logic [63:0] pc, logic [31:0] first, logic [31:0] second, int count = 2, bit keep0 = 1, bit keep1 = 1, int fetch_fault_lane = -1);
    if (keep0) expect_instruction(pc, first);
    if (count == 2 && keep1) expect_instruction(pc + 4, second);
    instructions.valid = 1;
    instructions.bits.count = 2'(count);
    instructions.bits.entries[0] = '{pc: pc, instruction: first, fault: '0};
    instructions.bits.entries[1] = '{pc: pc + 4, instruction: second, fault: '0};
    if (fetch_fault_lane >= 0)
      instructions.bits.entries[fetch_fault_lane].fault = '{valid: 1'b1, bits: '{cause: 64'd1, value: pc + 4*64'(fetch_fault_lane)}};
    #1;
    while (!ready) tick();
    tick();
    instructions.valid = 0;
  endtask
  task automatic drain;
    instructions.valid = 0;
    do tick(); while (expected.size() != 0 || expected_redirects.size() != 0 || expected_requests.size() != 0 || expected_completions.size() != 0 || response_count != 0);
    repeat (8) tick();
    assert (expected.size() == 0 && expected_redirects.size() == 0) else $fatal(1, "missing ordered outcomes");
  endtask
  task automatic stop_at(logic [63:0] pc, logic [63:0] target, int disposition = 0, logic [63:0] cause = 0, logic [63:0] value = 0);
    redirect_t item;
    item = '{pc: pc, target: disposition == 1 ? trap_target : target, resolution: '{disposition: 2'(disposition), cause: cause, value: value}};
    expected_redirects.push_back(item);
  endtask
  task automatic reset_core;
    canceled += response_count;
    reset = 1; instructions = '0; inject_enable = 0; inject_pc = 0; inject_result = '0;
    interrupts = 0; trap_target = 0;
    block_requests = 0; block_stores = 0; inject_memory_fault = 0; hold_responses = 0; lookup_mode = 0;
    expected.delete(); expected_redirects.delete(); expected_requests.delete(); expected_completions.delete(); response_owners.delete();
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
    item='0; item.fetched.pc=pc; item.fetched.instruction=word;
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

    // Same-group WAW splits; youngest of several older writers wins subsequent RAW.
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

    // Real instruction faults and injected MEM fault/replay preserve the successful prefix.
    stop_at('h2500, 'h2500, 1, 0, 'h2502);
    send('h2500, jump(10, 2), imm(11, 0, 99), 2, 0, 0);
    drain();
    stop_at('h2510, 'h2510, 1, 2, 64'hffffffff);
    send('h2510, 32'hffffffff, imm(11, 0, 99), 2, 0, 0);
    drain();
    stop_at('h2530, 'h2530, 1, 0, 'h602);
    send('h2530, imm(10, 5, 2, 0, 'h67), '0, 1, 0, 0);
    drain();
    stop_at('h2542, 'h2542, 1, 0, 'h2542);
    send('h2542, imm(10, 0, 1), '0, 1, 0, 0);
    drain();
    inject_enable = 1; inject_pc = 'h2550;
    inject_result = '{disposition: 2'd1, cause: 64'd5, value: 64'hdead};
    stop_at('h2550, 'h2550, 1, 2, 64'hffffffff);
    send('h2550, 32'hffffffff, '0, 1, 0, 0);
    drain(); inject_enable = 0;
    for (int lane = 0; lane < 2; lane++) begin
      for (int action = 1; action <= 2; action++) begin
        logic [63:0] pc;
        pc = 64'('h2600 + 64*lane + 16*action);
        inject_enable = 1; inject_pc = pc + 64'(4*lane);
        inject_result = '{disposition: 2'(action), cause: 64'd13, value: 64'h3000};
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
    inject_result = '{disposition: 2'd1, cause: 64'd5, value: 64'hdead};
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
    assert (single_issues > 10 && stops == 20) else $fatal(1, "insufficient hazard/stop coverage: single=%0d stops=%0d", single_issues, stops);

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

    // Natural alignment faults occur before lookup, including faults on x0 loads.
    begin
      int before_lookups;
      before_lookups = lookups;
      for (int width = 1; width < 4; width++) begin
        stop_at('h4e40, 'h4e40, 1, 4, 'h301);
        send('h4e40, imm(0, 1, 1, width, 'h03), imm(16, 0, 99), 2, 0, 0); drain();
        stop_at('h4e54, 'h4e54, 1, 6, 'h301);
        send('h4e50, imm(16, 0, 8), store(2, 1, 1, width), 2, 1, 0); drain();
      end
      assert (lookups == before_lookups) else $fatal(1, "misalignment issued speculative lookup");
    end

    // An older stop prevents a younger store, including a speculative owned-store candidate.
    lookup_mode = 1;
    stop_at('h4f00, 'h4f20);
    send('h4f00, jump(16, 32), store(2, 1, 0, 3), 2, 1, 0); drain();
    inject_enable = 1; inject_pc = 'h4f40; inject_result = '{disposition: 2'd1, cause: 64'd2, value: 64'hdead};
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
    inject_enable = 1; inject_pc = 'h5104; inject_result = '{disposition: 2'd1, cause: 64'd2, value: 64'hbad};
    stop_at('h5104, 'h5104, 1, 2, 'hbad);
    send('h5100, imm(11, 1, 8, 3, 'h03), imm(20, 0, 7), 2, 1, 0);
    repeat (10) tick();
    assert (expected_redirects.size() == 1 && response_count == 1 && expected.size() == 0) else $fatal(1, "fault did not retain precise drain boundary");
    hold_responses = 0; drain(); inject_enable = 0;

    // Fetch faults ignore even valid memory encodings and preserve both age slots.
    // In particular, a younger fault must wait for its older accepted load.
    begin
      int before_lookups = lookups;
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
      logic [63:0] pc = 64'('h51c0 + mode*16);
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
      int before_override=wb_overrides;
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
    $display("Memory: %0d accepted, %0d responses, %0d reset-canceled, %0d stores, max %0d outstanding, %0d overlap retirements, %0d shared-write cycles", requests, responses, canceled, stores, max_outstanding, overlap_retirements, shared_writes);
    $display("RV2Wide passed: %0d retirements, %0d dual cycles, %0d stops", commits, dual_commits, stops);
    $finish;
  end
endmodule
