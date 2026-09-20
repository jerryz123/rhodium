// Verifies coherent and ROM instruction snapshots, VIPT hits, refill errors, and FENCE.I cancellation.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_icache_tb;
  typedef struct packed { logic [63:0] address; } core_req_bits_t;
  typedef struct packed { logic valid; core_req_bits_t bits; } core_req_t;
  typedef struct packed { logic [63:0] address; logic [1:0] operation; } prefetch_bits_t;
  typedef struct packed { logic valid; prefetch_bits_t bits; } prefetch_t;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic [31:0] word; logic page_fault; logic access_fault; } instruction_bits_t;
  typedef struct packed { instruction_bits_t response; logic replay; } result_bits_t;
  typedef struct packed { logic valid; result_bits_t bits; } instruction_resp_t;
  typedef struct packed { logic flush; logic invalidate_all; logic s1_kill; core_req_t request; } core_in_t;
  typedef struct packed { instruction_resp_t response; } core_out_t;

  typedef struct packed { logic valid; CHIReqFlit bits; } req_forward_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_forward_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_forward_t;
  typedef struct packed { ready_t requester; rsp_forward_t response; } rsp_in_t;
  typedef struct packed { rsp_forward_t requester; ready_t response; } rsp_out_t;
  typedef struct packed { ready_t request; dat_forward_t response; } dat_in_t;
  typedef struct packed { dat_forward_t request; ready_t response; } dat_out_t;
  typedef struct packed { ready_t req; rsp_in_t rsp; dat_in_t dat; } chi_in_t;
  typedef struct packed { req_forward_t req; rsp_out_t rsp; dat_out_t dat; } chi_out_t;

  localparam logic [6:0] READ_ONCE = 7'h03;
  localparam logic [4:0] COMP_ACK = 5'h02;
  localparam logic [3:0] COMP_DATA = 4'h4;
  localparam logic [6:0] HOME_ID = 7'd1;
  localparam logic [6:0] CACHE_ID = 7'd2;

  logic clock = 1'b0;
  logic reset = 1'b1;
  logic [6:0] node_id = CACHE_ID;
  core_in_t core_in;
  core_out_t core_out;
  prefetch_t prefetch_in;
  typedef struct packed { logic valid; logic [63:0] bits; } lookup_t;
  lookup_t virtual_lookup_in;
  ready_t virtual_lookup_out;
  logic probe_only = 1'b0;
  logic lookup_override = 1'b0;
  lookup_t staged_lookup;
  logic [63:0] virtual_page_xor = 64'h4000_0000;
  assign virtual_lookup_in = lookup_override ? staged_lookup :
    lookup_t'({core_in.request.valid | probe_only, core_in.request.bits.address ^ virtual_page_xor});
  chi_in_t chi_in;
  chi_out_t chi_out;
  logic forbid_core_response = 1'b0;
  bit rom_phase = 0;

  RV5StageL1ICache dut (.*);
  always #5 clock = ~clock;

  task automatic tick;
    begin
      if (rom_phase) assert (!chi_out.rsp.requester.valid) else $fatal(1, "ROM read sent CompAck");
      if (forbid_core_response)
        assert (!core_out.response.valid)
          else $fatal(1, "L1I produced a response for a prefetch");
      @(posedge clock);
      #1;
    end
  endtask

  task automatic send_prefetch(input logic [63:0] address);
    begin
      prefetch_in.bits.address = address;
      prefetch_in.bits.operation = 2'd1;
      prefetch_in.valid = 1'b1;
      tick();
      prefetch_in = '0;
    end
  endtask

  task automatic grant_req_credit;
    begin
      chi_in.req.ready = 1'b1;
    end
  endtask

  task automatic grant_rsp_credit;
    begin
      chi_in.rsp.requester.ready = 1'b1;
    end
  endtask

  task automatic wait_dat_credit;
    integer cycles;
    begin
      cycles = 0;
      while (!chi_out.dat.response.ready && cycles < 50) begin
        tick();
        cycles = cycles + 1;
      end
      assert (chi_out.dat.response.ready)
        else $fatal(1, "L1I did not accept response data");
    end
  endtask


  task automatic send_core_request(input logic [63:0] address);
    integer cycles;
    begin
      cycles = 0;
      core_in.request.bits.address = address;
      probe_only = 1'b1;
      // Each retry is a new S0/S1/S2 attempt, not an eventual cache response.
      #1;
      while (!virtual_lookup_out.ready && cycles < 200) begin
        tick();
        cycles++;
      end
      assert (virtual_lookup_out.ready) else $fatal(1, "L1I virtual admission timed out");
      tick();
      probe_only = 1'b0;
      core_in.request.valid = 1'b1;
      tick();
      core_in.request.valid = 1'b0;
    end
  endtask

  task automatic accept_read_request(input logic [63:0] address);
    integer cycles;
    begin
      cycles = 0;
      while (!chi_out.req.valid && cycles < 100) begin
        tick();
        cycles = cycles + 1;
      end
      assert (chi_out.req.valid)
        else $fatal(1, "L1I did not issue ReadOnce");
      assert (chi_out.req.bits.opcode == READ_ONCE &&
              chi_out.req.bits.src_id == CACHE_ID &&
              chi_out.req.bits.tgt_id == HOME_ID &&
              chi_out.req.bits.txn_id == 12'd0 &&
              chi_out.req.bits.return_txn_id_or_stash_lpid == 12'd0 &&
              chi_out.req.bits.address == address[43:0] &&
              chi_out.req.bits.size_or_num_req == 6'd6 &&
              chi_out.req.bits.exp_comp_ack &&
              chi_out.req.bits.snp_attr_or_do_dwt &&
              chi_out.req.bits.mem_attr == 4'hd &&
              chi_out.req.bits.allow_retry &&
              chi_out.req.bits.pcrd_type == 4'd0)
        else $fatal(1, "L1I emitted malformed ReadOnce");
      tick();
    end
  endtask

  task automatic return_line(
    input logic [63:0] address,
    input logic [511:0] line,
    input int gap = 0,
    input logic [1:0] error = 0
  );
    integer packet;
    begin
      for (packet = 3; packet >= 0; packet = packet - 1) begin
        repeat (gap) tick();
        wait_dat_credit();
        chi_in.dat.response.bits = '0;
        chi_in.dat.response.bits.data = line[packet * 128 +: 128];
        chi_in.dat.response.bits.byte_enable = 16'hffff;
        chi_in.dat.response.bits.data_id = address[5:4] + packet[1:0];
        chi_in.dat.response.bits.resp = 3'b000;
        chi_in.dat.response.bits.resp_err = error;
        chi_in.dat.response.bits.opcode = COMP_DATA;
        chi_in.dat.response.bits.home_nid_or_pbha_or_mismatched_mecid = rom_phase ? 7'd4 : HOME_ID;
        chi_in.dat.response.bits.dbid_or_mecid = 16'h0055;
        chi_in.dat.response.bits.txn_id = 12'd0;
        chi_in.dat.response.bits.src_id = rom_phase ? 7'd4 : HOME_ID;
        chi_in.dat.response.bits.tgt_id = CACHE_ID;
        chi_in.dat.response.valid = 1'b1;
        tick();
        chi_in.dat.response.valid = 1'b0;
        chi_in.dat.response.bits = '0;
      end
    end
  endtask

  task automatic accept_comp_ack;
    integer cycles;
    begin
      cycles = 0;
      while (!chi_out.rsp.requester.valid && cycles < 100) begin
        tick();
        cycles = cycles + 1;
      end
      assert (chi_out.rsp.requester.valid)
        else $fatal(1, "L1I did not issue CompAck");
      assert (chi_out.rsp.requester.bits.opcode == COMP_ACK &&
              chi_out.rsp.requester.bits.src_id == CACHE_ID &&
              chi_out.rsp.requester.bits.tgt_id == HOME_ID &&
              chi_out.rsp.requester.bits.txn_id == 12'h055)
        else $fatal(1, "L1I emitted malformed CompAck");
      tick();
    end
  endtask

  task automatic accept_rom_request(input logic [63:0] address);
    for (int c = 0; !chi_out.req.valid && c < 100; c++) tick();
    assert (chi_out.req.valid && chi_out.req.bits.opcode == 7'h04 &&
            chi_out.req.bits.address == address[43:0] && chi_out.req.bits.size_or_num_req == 6 &&
            chi_out.req.bits.src_id == CACHE_ID && chi_out.req.bits.tgt_id == 4 &&
            !chi_out.req.bits.exp_comp_ack && !chi_out.req.bits.allow_retry && chi_out.req.bits.mem_attr == 0)
      else $fatal(1, "malformed ROM line read");
    tick();
  endtask

  task automatic wait_result;
    int attempts;
    attempts=0;
    while ((!core_out.response.valid || core_out.response.bits.replay) && attempts<100) begin
      send_core_request(core_in.request.bits.address);
      attempts++;
    end
    assert(core_out.response.valid && !core_out.response.bits.replay)
      else $fatal(1,"fetch replay did not resolve");
  endtask

  task automatic expect_instruction(input logic [31:0] instruction);
    integer cycles;
    begin
      wait_result();
      assert (core_out.response.valid &&
              !core_out.response.bits.response.page_fault &&
              !core_out.response.bits.response.access_fault &&
              core_out.response.bits.response.word == instruction)
        else $fatal(1, "instruction %h, expected %h",
                    core_out.response.bits.response.word, instruction);
      tick();
    end
  endtask

  task automatic invalidate_cache(input logic [63:0] address);
    core_in.invalidate_all = 1;
    tick();
    core_in.invalidate_all = 0;
  endtask

  localparam logic [63:0] ADDRESS = 64'h00000001_00000000;
  localparam logic [63:0] SECOND_ADDRESS = 64'h00000001_00000040;
  localparam logic [63:0] THIRD_ADDRESS = 64'h00000001_00000080;
  localparam logic [63:0] PREFETCH_ADDRESS = 64'h00000001_000000c0;
  localparam logic [511:0] LINE = {
    64'hffffffff_eeeeeeee,
    64'hdddddddd_cccccccc,
    64'hbbbbbbbb_aaaaaaaa,
    64'h99999999_00000000,
    64'h88888888_77777777,
    64'h66666666_55555555,
    64'h44444444_33333333,
    64'h22222222_11111111
  };
  // These lines collide in set zero when the fixture uses four sets.
  localparam logic [63:0] COLLIDE_B_ADDRESS = ADDRESS + 64'h100;
  localparam logic [63:0] COLLIDE_C_ADDRESS = ADDRESS + 64'h200;
  localparam logic [511:0] LINE_B = {LINE[511:32], 32'hb1b1b1b1};
  localparam logic [511:0] LINE_C = {LINE[511:32], 32'hc1c1c1c1};

  initial begin
    core_in = '0;
    prefetch_in = '0;
    chi_in = '0;
    repeat (2) tick();
    reset = 1'b0;
    grant_req_credit();
    grant_rsp_credit();

    // A translation miss/fault supplies an index but no physical resolution.
    probe_only = 1'b1;
    core_in.request.bits.address = ADDRESS;
    repeat (4) begin
      tick();
      assert (!core_out.response.valid && !chi_out.req.valid)
        else $fatal(1, "unresolved virtual lookup caused a response or refill");
    end
    probe_only = 1'b0;

    forbid_core_response = 1'b1;
    send_prefetch(PREFETCH_ADDRESS);
    accept_read_request(PREFETCH_ADDRESS);
    return_line(PREFETCH_ADDRESS, LINE);
    accept_comp_ack();
    repeat (12) tick();
    forbid_core_response = 1'b0;
    send_core_request(PREFETCH_ADDRESS);
    expect_instruction(32'h11111111);

    virtual_page_xor = 64'h8000_0000;
    send_core_request(PREFETCH_ADDRESS);
    expect_instruction(32'h11111111);

    send_core_request(ADDRESS);
    accept_read_request(ADDRESS);
    return_line(ADDRESS, LINE);
    accept_comp_ack();
    expect_instruction(32'h11111111);

    send_core_request(ADDRESS + 64'd4);
    expect_instruction(32'h22222222);
    send_core_request(ADDRESS + 64'd8);
    expect_instruction(32'h33333333);
    send_core_request(ADDRESS + 64'd12);
    expect_instruction(32'h44444444);

    // Different S0 and S1 addresses coexist: physical tag resolution must use
    // the preceding SRAM read, while a new virtual read enters every cycle.
    lookup_override = 1'b1;
    staged_lookup = '{valid: 1'b1, bits: ADDRESS ^ virtual_page_xor};
    #1;
    assert (virtual_lookup_out.ready);
    tick();
    core_in.request = '{valid: 1'b1, bits: '{address: ADDRESS}};
    staged_lookup.bits = (ADDRESS + 64'd4) ^ virtual_page_xor;
    #1;
    assert (virtual_lookup_out.ready);
    tick();
    assert (core_out.response.valid && core_out.response.bits.response.word == 32'h11111111)
      else $fatal(1, "S2 did not retain the first pipelined hit");
    core_in.request.bits.address = ADDRESS + 64'd4;
    staged_lookup.valid = 1'b0;
    #1;
    tick();
    core_in.request.valid = 1'b0;
    assert (core_out.response.valid && core_out.response.bits.response.word == 32'h22222222)
      else $fatal(1, "S0/S1 address overlap corrupted a consecutive hit");
    tick();
    lookup_override = 1'b0;

    // Response buffering belongs to the frontend. S1 kill must instead discard
    // a younger lookup without altering a preceding completed S2 result.
    probe_only = 1;
    core_in.request.bits.address = ADDRESS;
    tick();
    probe_only = 0;
    core_in.request.valid = 1;
    core_in.s1_kill = 1;
    tick();
    core_in.request.valid = 0;
    core_in.s1_kill = 0;
    assert(!core_out.response.valid) else $fatal(1,"killed S1 lookup escaped");

    grant_rsp_credit();
    invalidate_cache(ADDRESS);
    grant_req_credit();
    grant_rsp_credit();
    send_core_request(ADDRESS);
    accept_read_request(ADDRESS);
    return_line(ADDRESS, LINE);
    accept_comp_ack();
    expect_instruction(32'h11111111);

    // A speculative flush preserves residency, while architectural
    // invalidation forces the next request back through CHI.
    probe_only = 1'b1;
    core_in.request.bits.address = ADDRESS + 64'd4;
    core_in.flush = 1'b1;
    #1;
    assert (virtual_lookup_out.ready) else $fatal(1, "flush blocked the replacement S0 lookup");
    tick();
    core_in.flush = 1'b0;
    probe_only = 1'b0;
    core_in.request.valid = 1'b1;
    tick();
    core_in.request.valid = 1'b0;
    expect_instruction(32'h22222222);
    probe_only = 1'b1;
    core_in.request.bits.address = ADDRESS;
    core_in.invalidate_all = 1'b1;
    #1;
    assert (virtual_lookup_out.ready) else $fatal(1, "invalidation blocked the replacement S0 lookup");
    tick();
    core_in.invalidate_all = 1'b0;
    probe_only = 1'b0;
    core_in.request.valid = 1'b1;
    tick();
    core_in.request.valid = 1'b0;
    grant_req_credit();
    grant_rsp_credit();
    accept_read_request(ADDRESS);
    return_line(ADDRESS, LINE);
    accept_comp_ack();
    expect_instruction(32'h11111111);

    // An invalidated in-flight refill may drain, but cannot respond or install.
    grant_req_credit();
    grant_rsp_credit();
    send_core_request(SECOND_ADDRESS);
    accept_read_request(SECOND_ADDRESS);
    core_in.invalidate_all = 1'b1;
    tick();
    core_in.invalidate_all = 1'b0;
    return_line(SECOND_ADDRESS, LINE);
    accept_comp_ack();
    repeat (2) tick();
    assert (!core_out.response.valid)
      else $fatal(1, "invalidated refill returned an instruction");
    grant_req_credit();
    grant_rsp_credit();
    send_core_request(SECOND_ADDRESS);
    accept_read_request(SECOND_ADDRESS);
    return_line(SECOND_ADDRESS, LINE);
    accept_comp_ack();
    expect_instruction(32'h11111111);

    // A speculative flush drops the requester token but may retain the line
    // installed by the already-issued coherent transaction.
    grant_req_credit();
    grant_rsp_credit();
    send_core_request(THIRD_ADDRESS);
    accept_read_request(THIRD_ADDRESS);
    core_in.flush = 1'b1;
    tick();
    core_in.flush = 1'b0;
    return_line(THIRD_ADDRESS, LINE);
    accept_comp_ack();
    repeat (12) begin
      tick();
      assert (!core_out.response.valid)
        else $fatal(1, "flushed refill returned an instruction");
    end
    send_core_request(THIRD_ADDRESS);
    expect_instruction(32'h11111111);

    // Two colliding lines coexist without disturbing one another.
    core_in.invalidate_all = 1'b1;
    tick();
    core_in.invalidate_all = 1'b0;
    grant_req_credit();
    grant_rsp_credit();
    send_core_request(ADDRESS);
    accept_read_request(ADDRESS);
    return_line(ADDRESS, LINE);
    accept_comp_ack();
    expect_instruction(32'h11111111);
    send_core_request(COLLIDE_B_ADDRESS);
    accept_read_request(COLLIDE_B_ADDRESS);
    return_line(COLLIDE_B_ADDRESS, LINE_B);
    accept_comp_ack();
    expect_instruction(32'hb1b1b1b1);
    send_core_request(ADDRESS);
    expect_instruction(32'h11111111);
    send_core_request(COLLIDE_B_ADDRESS);
    expect_instruction(32'hb1b1b1b1);
    // With both ways occupied, the round-robin pointer replaces the first
    // way. The second colliding line remains a hit while the original misses.
    send_core_request(COLLIDE_C_ADDRESS);
    accept_read_request(COLLIDE_C_ADDRESS);
    return_line(COLLIDE_C_ADDRESS, LINE_C);
    accept_comp_ack();
    expect_instruction(32'hc1c1c1c1);
    send_core_request(COLLIDE_B_ADDRESS);
    expect_instruction(32'hb1b1b1b1);
    send_core_request(COLLIDE_C_ADDRESS);
    expect_instruction(32'hc1c1c1c1);
    grant_req_credit();
    grant_rsp_credit();
    send_core_request(ADDRESS);
    accept_read_request(ADDRESS);
    return_line(ADDRESS, LINE);
    accept_comp_ack();
    expect_instruction(32'h11111111);

    // One virtual address can resolve to different physical pages. Both tags
    // must coexist without a false hit, and switching back must recover A.
    virtual_page_xor = 64'h4000_1000;
    send_core_request(ADDRESS + 64'h10c0);
    accept_read_request(ADDRESS + 64'h10c0);
    return_line(ADDRESS + 64'h10c0, LINE_B);
    accept_comp_ack();
    expect_instruction(32'hb1b1b1b1);
    virtual_page_xor = 64'h4000_2000;
    send_core_request(ADDRESS + 64'h20c0);
    accept_read_request(ADDRESS + 64'h20c0);
    return_line(ADDRESS + 64'h20c0, LINE_C);
    accept_comp_ack();
    expect_instruction(32'hc1c1c1c1);
    virtual_page_xor = 64'h4000_1000;
    send_core_request(ADDRESS + 64'h10c0);
    expect_instruction(32'hb1b1b1b1);

    // Architectural invalidation discards resident code; every new
    // instruction must come entirely from the post-fence refill.
    for (int offset = 0; offset < 64; offset += 4) begin
      core_in.invalidate_all = 1;
      tick();
      core_in.invalidate_all = 0;
      grant_req_credit();
      grant_rsp_credit();
      send_core_request(ADDRESS + 64'(offset));
      accept_read_request(ADDRESS);
      forbid_core_response = 1;
      return_line(ADDRESS, {16{32'h55aaaa55}}, offset % 3);
      forbid_core_response = 0;
      accept_comp_ack();
      wait_result();
      assert (core_out.response.valid && core_out.response.bits.response.word == 32'h55aaaa55)
        else $fatal(1, "old aligned instruction did not complete");
      grant_rsp_credit();
      invalidate_cache(ADDRESS);
      grant_req_credit();
      grant_rsp_credit();
      send_core_request(ADDRESS + 64'(offset));
      accept_read_request(ADDRESS);
      return_line(ADDRESS, {16{32'haa5555aa}}, offset % 3);
      accept_comp_ack();
      repeat (4) tick();
      expect_instruction(32'haa5555aa);
    end
    // Requests and CompAck remain stable under stalls; a retry retains the snapshot context.
    invalidate_cache(ADDRESS);
    chi_in.req.ready = 0;
    send_core_request(ADDRESS);
    for (int c = 0; !chi_out.req.valid && c < 100; c++) tick();
    begin
      CHIReqFlit saved;
      saved = chi_out.req.bits;
      repeat (4) begin
        tick();
        assert (chi_out.req.valid && chi_out.req.bits == saved) else $fatal(1, "stalled read changed");
      end
    end
    chi_in.req.ready = 1;
    accept_read_request(ADDRESS);
    for (int event_index = 0; event_index < 2; event_index++) begin
      chi_in.rsp.response.bits = '0;
      chi_in.rsp.response.bits.opcode = event_index == 0 ? 5'h03 : 5'h07;
      chi_in.rsp.response.bits.src_id = HOME_ID;
      chi_in.rsp.response.bits.tgt_id = CACHE_ID;
      chi_in.rsp.response.bits.pcrd_type = 2;
      chi_in.rsp.response.valid = 1;
      #1;
      for (int c = 0; !chi_out.rsp.response.ready && c < 100; c++) tick();
      assert (chi_out.rsp.response.ready) else $fatal(1, "retry response blocked");
      tick();
      chi_in.rsp.response.valid = 0;
      chi_in.req.ready = 0;
    end
    for (int c = 0; !chi_out.req.valid && c < 100; c++) tick();
    assert (chi_out.req.valid && chi_out.req.bits.opcode == READ_ONCE &&
            chi_out.req.bits.address == ADDRESS[43:0] && !chi_out.req.bits.allow_retry &&
            chi_out.req.bits.pcrd_type == 2) else $fatal(1, "retry lost line or credit");
    chi_in.req.ready = 1;
    tick();
    chi_in.rsp.requester.ready = 0;
    return_line(ADDRESS, LINE);
    for (int c = 0; !chi_out.rsp.requester.valid && c < 100; c++) tick();
    begin
      CHIRspFlit saved;
      saved = chi_out.rsp.requester.bits;
      repeat (4) begin
        tick();
        assert (chi_out.rsp.requester.valid && chi_out.rsp.requester.bits == saved) else $fatal(1, "stalled acknowledgement changed");
      end
    end
    chi_in.rsp.requester.ready = 1;
    accept_comp_ack();
    // Invalidation during SRAM installation must prevent publishing the partial line.
    repeat (2) tick();
    invalidate_cache(ADDRESS);
    repeat (12) begin
      tick();
      assert (!core_out.response.valid) else $fatal(1, "fenced installation returned old code");
    end

    // A failed snapshot responds with an access fault and never becomes a cache hit.
    invalidate_cache(ADDRESS);
    send_core_request(ADDRESS);
    accept_read_request(ADDRESS);
    return_line(ADDRESS, LINE, 1, 2'b10);
    accept_comp_ack();
    wait_result();
    assert (core_out.response.valid && core_out.response.bits.response.access_fault)
      else $fatal(1, "read error was not delivered to Fetch");
    tick();
    send_core_request(ADDRESS);
    accept_read_request(ADDRESS);
    return_line(ADDRESS, LINE);
    accept_comp_ack();
    expect_instruction(32'h11111111);
    // ROM lines allocate locally but never acquire coherence ownership or send CompAck.
    invalidate_cache(ADDRESS);
    rom_phase = 1;
    chi_in.rsp.requester.ready = 0;
    chi_in.req.ready = 0;
    send_core_request(64'h10000);
    for (int c = 0; !chi_out.req.valid && c < 100; c++) tick();
    begin
      CHIReqFlit saved;
      saved = chi_out.req.bits;
      repeat (4) begin
        tick();
        assert (chi_out.req.valid && chi_out.req.bits == saved) else $fatal(1, "stalled ROM request changed");
      end
    end
    chi_in.req.ready = 1;
    accept_rom_request(64'h10000);
    return_line(64'h10000, LINE, 2);
    wait_result();
    expect_instruction(32'h11111111);
    for (int offset = 15; offset >= 0; offset--) begin
      send_core_request(64'h10000 + 64'(offset * 4));
      expect_instruction(LINE[offset * 32 +: 32]);
      assert (!chi_out.req.valid) else $fatal(1, "resident ROM line refetched");
    end
    send_core_request(64'h10040);
    accept_rom_request(64'h10040);
    return_line(64'h10040, LINE_B, 1, 2'b10);
    wait_result();
    assert (core_out.response.valid && core_out.response.bits.response.access_fault) else $fatal(1, "ROM error lost");
    tick();
    send_core_request(64'h10040);
    accept_rom_request(64'h10040);
    return_line(64'h10040, LINE_B);
    expect_instruction(32'hb1b1b1b1);
    invalidate_cache(64'h10000);
    send_core_request(64'h10000);
    accept_rom_request(64'h10000);
    core_in.invalidate_all = 1;
    tick();
    core_in.invalidate_all = 0;
    return_line(64'h10000, LINE, 1);
    repeat (15) begin
      tick();
      assert (!core_out.response.valid) else $fatal(1, "invalidated ROM refill escaped");
    end
    send_core_request(64'h10000);
    accept_rom_request(64'h10000);
    return_line(64'h10000, LINE);
    expect_instruction(32'h11111111);
    // Ordinary redirect during installation drains and keeps the snapshot,
    // while architectural invalidation during installation must discard it.
    for (int architectural = 0; architectural < 2; architectural++) begin
      invalidate_cache(64'h10000);
      send_core_request(64'h10000);
      accept_rom_request(64'h10000);
      return_line(64'h10000, LINE);
      repeat (3) tick();
      if (architectural != 0) core_in.invalidate_all = 1;
      else core_in.flush = 1;
      tick();
      core_in.invalidate_all = 0;
      core_in.flush = 0;
      repeat (15) begin
        tick();
        assert (!core_out.response.valid) else $fatal(1, "canceled ROM installation returned an instruction");
      end
      send_core_request(64'h10000);
      if (architectural != 0) begin
        accept_rom_request(64'h10000);
        return_line(64'h10000, LINE);
      end
      expect_instruction(32'h11111111);
      assert (!chi_out.req.valid) else $fatal(1, "redirect discarded a resident ROM line");
    end
    $display("Cached ROM line hits, boundary crossing, replay, errors, and invalidation passed");
    $display("Ziccif aligned-word visibility passed: 16 offsets, reordered/gapped refills, replay across invalidation");
    $display("RV5Stage VIPT instruction-cache simulation passed");
    $finish;
  end
endmodule
