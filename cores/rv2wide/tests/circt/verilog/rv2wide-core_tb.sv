// SPDX-License-Identifier: Apache-2.0
module rv2wide_core_tb;
  typedef struct packed { logic [63:0] pc; logic [31:0] instruction; } instruction_t;
  typedef struct packed { logic [1:0] count; instruction_t [1:0] entries; } packet_t;
  typedef struct packed { logic valid; packet_t bits; } packet_flow_t;
  typedef struct packed { logic [1:0] disposition; logic [63:0] cause, value; } resolution_t;
  typedef struct packed { logic valid; resolution_t bits; } resolution_flow_t;
  typedef struct packed { logic valid; instruction_t bits; } instruction_flow_t;
  typedef struct packed { instruction_t fetched; logic [4:0] rd; logic write; logic [63:0] data; } retirement_t;
  typedef struct packed { logic valid; retirement_t bits; } retirement_flow_t;
  typedef struct packed { logic [63:0] pc, target; resolution_t resolution; } redirect_t;
  typedef struct packed { logic valid; redirect_t bits; } redirect_flow_t;

  logic clock = 0, reset = 1;
  packet_flow_t instructions;
  logic ready;
  resolution_flow_t resolution[2];
  instruction_flow_t memory_stage[2];
  retirement_flow_t retired[2];
  redirect_flow_t redirect;
  logic [1:0] issued, retired_count;
  logic inject_enable;
  logic [63:0] inject_pc;
  resolution_t inject_result;
  retirement_t expected[$];
  redirect_t expected_redirects[$];
  logic [63:0] model[32];
  integer cycles = 0, commits = 0, dual_commits = 0, single_issues = 0, stops = 0;
  integer dual_run = 0, longest_dual_run = 0;
  bit saw_repacked = 0;

  RV2WideCore dut(
    .clock(clock), .reset(reset), .instructions_in(instructions), .instructions_out(ready),
    .resolution_0_in(resolution[0]), .resolution_1_in(resolution[1]),
    .memory_stage_0_out(memory_stage[0]), .memory_stage_1_out(memory_stage[1]),
    .retired_0_out(retired[0]), .retired_1_out(retired[1]),
    .redirect_out(redirect), .issued(issued), .retired_count(retired_count)
  );
  always #5 clock = ~clock;
  always_comb begin
    for (int lane = 0; lane < 2; lane++) begin
      resolution[lane] = '0;
      if (inject_enable && memory_stage[lane].valid && memory_stage[lane].bits.pc == inject_pc) begin
        resolution[lane].valid = 1;
        resolution[lane].bits = inject_result;
      end
    end
  end

  always @(posedge clock) begin
    if (!reset) begin
      cycles++;
      if (cycles > 3000) $fatal(1, "watchdog");
      assert (issued <= 2 && retired_count <= 2) else $fatal(1, "non-prefix count");
      assert (!retired[1].valid || retired[0].valid) else $fatal(1, "younger retired alone");
      if (retired[0].valid && retired[1].valid && retired[0].bits.write && retired[1].bits.write)
        assert (retired[0].bits.rd != retired[1].bits.rd) else $fatal(1, "same-group WAW was not split");
      assert (int'(retired_count) == int'(retired[0].valid) + int'(retired[1].valid)) else $fatal(1, "retirement count mismatch");
      if (issued == 1) single_issues++;
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
          if (want.write)
            assert (retired[lane].bits.rd == want.rd && retired[lane].bits.data == want.data)
              else $fatal(1, "result pc=%h rd=%d got=%h expected rd=%d data=%h", want.fetched.pc, retired[lane].bits.rd, retired[lane].bits.data, want.rd, want.data);
          commits++;
        end
      end
      if (redirect.valid) begin
        redirect_t want;
        assert (expected_redirects.size() > 0) else $fatal(1, "unexpected redirect pc=%h", redirect.bits.pc);
        want = expected_redirects.pop_front();
        assert (redirect.bits.pc == want.pc && redirect.bits.target == want.target && redirect.bits.resolution.disposition == want.resolution.disposition)
          else $fatal(1, "redirect mismatch got=%h want=%h", redirect.bits, want);
        if (want.resolution.disposition == 1)
          assert (redirect.bits.resolution.cause == want.resolution.cause && redirect.bits.resolution.value == want.resolution.value)
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
  task automatic send(logic [63:0] pc, logic [31:0] first, logic [31:0] second, int count = 2, bit keep0 = 1, bit keep1 = 1);
    if (keep0) expect_instruction(pc, first);
    if (count == 2 && keep1) expect_instruction(pc + 4, second);
    instructions.valid = 1;
    instructions.bits.count = 2'(count);
    instructions.bits.entries[0] = '{pc: pc, instruction: first};
    instructions.bits.entries[1] = '{pc: pc + 4, instruction: second};
    #1;
    while (!ready) tick();
    tick();
    instructions.valid = 0;
  endtask
  task automatic drain;
    instructions.valid = 0;
    repeat (12) tick();
    assert (expected.size() == 0 && expected_redirects.size() == 0) else $fatal(1, "missing ordered outcomes");
  endtask
  task automatic stop_at(logic [63:0] pc, logic [63:0] target, int disposition = 0, logic [63:0] cause = 0, logic [63:0] value = 0);
    redirect_t item;
    item = '{pc: pc, target: target, resolution: '{disposition: 2'(disposition), cause: cause, value: value}};
    expected_redirects.push_back(item);
  endtask
  task automatic reset_core;
    reset = 1; instructions = '0; inject_enable = 0; inject_pc = 0; inject_result = '0;
    expected.delete(); expected_redirects.delete();
    for (int i = 0; i < 32; i++) model[i] = 0;
    repeat (2) tick();
    reset = 0;
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
    $display("RV2Wide passed: %0d retirements, %0d dual cycles, %0d stops", commits, dual_commits, stops);
    $finish;
  end
endmodule
