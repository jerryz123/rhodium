// Verifies staged VIPT reads and physical resolutions, aliases, refills, backpressure, and snoops.
module rv5stage_icache_tb;
  typedef struct packed { logic [63:0] address; } core_req_bits_t;
  typedef struct packed { logic valid; core_req_bits_t bits; } core_req_t;
  typedef struct packed { logic [63:0] address; logic [1:0] operation; } prefetch_bits_t;
  typedef struct packed { logic valid; prefetch_bits_t bits; } prefetch_t;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic [31:0] word; logic page_fault; logic access_fault; } instruction_bits_t;
  typedef struct packed { logic valid; instruction_bits_t bits; } instruction_resp_t;
  typedef struct packed { logic flush; logic invalidate_all; core_req_t request; ready_t response; } core_in_t;
  typedef struct packed { ready_t request; instruction_resp_t response; } core_out_t;

  typedef struct packed { logic valid; CHIReqFlit bits; } req_forward_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_forward_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_forward_t;
  typedef struct packed { logic valid; CHISnpFlit bits; } snp_forward_t;
  typedef struct packed {
    ready_t requests;
    ready_t requester_responses;
    ready_t request_data;
    rsp_forward_t responses;
    dat_forward_t response_data;
    snp_forward_t snoops;
  } chi_in_t;
  typedef struct packed {
    req_forward_t requests;
    rsp_forward_t requester_responses;
    dat_forward_t request_data;
    ready_t responses;
    ready_t response_data;
    ready_t snoops;
  } chi_out_t;

  localparam logic [6:0] READ_CLEAN = 7'h02;
  localparam logic [4:0] SNP_RESP = 5'h01;
  localparam logic [4:0] COMP_ACK = 5'h02;
  localparam logic [4:0] SNP_SHARED = 5'h01;
  localparam logic [4:0] SNP_MAKE_INVALID = 5'h0a;
  localparam logic [4:0] SNP_DVM_OP = 5'h0d;
  localparam logic [4:0] SNP_QUERY = 5'h10;
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

  RV5StageL1ICache dut (.*);
  always #5 clock = ~clock;

  task automatic tick;
    begin
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
      chi_in.requests.ready = 1'b1;
    end
  endtask

  task automatic grant_rsp_credit;
    begin
      chi_in.requester_responses.ready = 1'b1;
    end
  endtask

  task automatic wait_dat_credit;
    integer cycles;
    begin
      cycles = 0;
      while (!chi_out.response_data.ready && cycles < 50) begin
        tick();
        cycles = cycles + 1;
      end
      assert (chi_out.response_data.ready)
        else $fatal(1, "L1I did not accept response data");
    end
  endtask

  task automatic wait_snp_credit;
    integer cycles;
    begin
      cycles = 0;
      while (!chi_out.snoops.ready && cycles < 50) begin
        tick();
        cycles = cycles + 1;
      end
      assert (chi_out.snoops.ready)
        else $fatal(1, "L1I did not accept a snoop");
    end
  endtask

  task automatic send_core_request(input logic [63:0] address);
    integer cycles;
    begin
      cycles = 0;
      core_in.request.bits.address = address;
      probe_only = 1'b1;
      // Launch S0 before expecting S1 physical acceptance; retry a blocked read.
      tick();
      probe_only = 1'b0;
      core_in.request.valid = 1'b1;
      while (!core_out.request.ready && cycles < 100) begin
        tick();
        cycles = cycles + 1;
      end
      assert (core_out.request.ready)
        else $fatal(1, "L1I did not accept a core request");
      tick();
      core_in.request.valid = 1'b0;
    end
  endtask

  task automatic accept_read_request(input logic [63:0] address);
    integer cycles;
    begin
      cycles = 0;
      while (!chi_out.requests.valid && cycles < 100) begin
        tick();
        cycles = cycles + 1;
      end
      assert (chi_out.requests.valid)
        else $fatal(1, "L1I did not issue ReadClean");
      assert (chi_out.requests.bits.opcode == READ_CLEAN &&
              chi_out.requests.bits.src_id == CACHE_ID &&
              chi_out.requests.bits.tgt_id == HOME_ID &&
              chi_out.requests.bits.txn_id == 12'd0 &&
              chi_out.requests.bits.return_txn_id_or_stash_lpid == 12'd0 &&
              chi_out.requests.bits.address == address[43:0] &&
              chi_out.requests.bits.size_or_num_req == 6'd6 &&
              chi_out.requests.bits.exp_comp_ack &&
              chi_out.requests.bits.snp_attr_or_do_dwt &&
              chi_out.requests.bits.mem_attr == 4'hd &&
              chi_out.requests.bits.allow_retry &&
              chi_out.requests.bits.pcrd_type == 4'd0)
        else $fatal(1, "L1I emitted malformed ReadClean");
      tick();
    end
  endtask

  task automatic return_line(
    input logic [63:0] address,
    input logic [511:0] line
  );
    integer packet;
    begin
      for (packet = 3; packet >= 0; packet = packet - 1) begin
        wait_dat_credit();
        chi_in.response_data.bits = '0;
        chi_in.response_data.bits.data = line[packet * 128 +: 128];
        chi_in.response_data.bits.byte_enable = 16'hffff;
        chi_in.response_data.bits.data_id = address[5:4] + packet[1:0];
        chi_in.response_data.bits.resp = 3'b001;
        chi_in.response_data.bits.opcode = COMP_DATA;
        chi_in.response_data.bits.home_nid_or_pbha_or_mismatched_mecid = HOME_ID;
        chi_in.response_data.bits.dbid_or_mecid = 16'h0055;
        chi_in.response_data.bits.txn_id = 12'd0;
        chi_in.response_data.bits.src_id = HOME_ID;
        chi_in.response_data.bits.tgt_id = CACHE_ID;
        chi_in.response_data.valid = 1'b1;
        tick();
        chi_in.response_data.valid = 1'b0;
        chi_in.response_data.bits = '0;
      end
    end
  endtask

  task automatic accept_comp_ack;
    integer cycles;
    begin
      cycles = 0;
      while (!chi_out.requester_responses.valid && cycles < 100) begin
        tick();
        cycles = cycles + 1;
      end
      assert (chi_out.requester_responses.valid)
        else $fatal(1, "L1I did not issue CompAck");
      assert (chi_out.requester_responses.bits.opcode == COMP_ACK &&
              chi_out.requester_responses.bits.src_id == CACHE_ID &&
              chi_out.requester_responses.bits.tgt_id == HOME_ID &&
              chi_out.requester_responses.bits.txn_id == 12'h055)
        else $fatal(1, "L1I emitted malformed CompAck");
      tick();
    end
  endtask

  task automatic expect_instruction(input logic [31:0] instruction);
    integer cycles;
    begin
      cycles = 0;
      while (!core_out.response.valid && cycles < 100) begin
        tick();
        cycles = cycles + 1;
      end
      assert (core_out.response.valid &&
              !core_out.response.bits.page_fault &&
              !core_out.response.bits.access_fault &&
              core_out.response.bits.word == instruction)
        else $fatal(1, "instruction %h, expected %h",
                    core_out.response.bits.word, instruction);
      tick();
    end
  endtask

  task automatic send_snoop(
    input logic [63:0] address,
    input logic [4:0] opcode,
    input logic [11:0] txn_id,
    input logic ret_to_src
  );
    begin
      wait_snp_credit();
      chi_in.snoops.bits = '0;
      chi_in.snoops.bits.address = address[43:3];
      chi_in.snoops.bits.opcode = opcode;
      chi_in.snoops.bits.txn_id = txn_id;
      chi_in.snoops.bits.src_id = HOME_ID;
      chi_in.snoops.bits.ret_to_src = ret_to_src;
      chi_in.snoops.valid = 1'b1;
      tick();
      chi_in.snoops.valid = 1'b0;
      chi_in.snoops.bits = '0;
    end
  endtask

  task automatic expect_snoop_response(
    input logic [11:0] txn_id,
    input logic [2:0] response
  );
    integer cycles;
    begin
      cycles = 0;
      while (!chi_out.requester_responses.valid && cycles < 100) begin
        tick();
        cycles = cycles + 1;
      end
      assert (chi_out.requester_responses.valid &&
              chi_out.requester_responses.bits.opcode == SNP_RESP &&
              chi_out.requester_responses.bits.txn_id == txn_id &&
              chi_out.requester_responses.bits.src_id == CACHE_ID &&
              chi_out.requester_responses.bits.tgt_id == HOME_ID &&
              chi_out.requester_responses.bits.resp == response)
        else $fatal(1, "L1I emitted malformed clean snoop response");
      tick();
    end
  endtask

  task automatic invalidate_cache(input logic [63:0] address);
    begin
      send_snoop(address, SNP_MAKE_INVALID, 12'h077, 1'b0);
      expect_snoop_response(12'h077, 3'd0);
    end
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
    core_in.response.ready = 1'b1;
    repeat (2) tick();
    reset = 1'b0;
    grant_req_credit();
    grant_rsp_credit();

    // A translation miss/fault supplies an index but no physical resolution.
    probe_only = 1'b1;
    core_in.request.bits.address = ADDRESS;
    repeat (4) begin
      tick();
      assert (!core_out.response.valid && !chi_out.requests.valid)
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
    assert (core_out.request.ready && virtual_lookup_out.ready);
    tick();
    assert (core_out.response.valid && core_out.response.bits.word == 32'h11111111)
      else $fatal(1, "S2 did not retain the first pipelined hit");
    core_in.request.bits.address = ADDRESS + 64'd4;
    staged_lookup.valid = 1'b0;
    #1;
    assert (core_out.request.ready);
    tick();
    core_in.request.valid = 1'b0;
    assert (core_out.response.valid && core_out.response.bits.word == 32'h22222222)
      else $fatal(1, "S0/S1 address overlap corrupted a consecutive hit");
    tick();
    lookup_override = 1'b0;

    // Fill both reserved response slots, then prove that releasing one does
    // not create a combinational response-ready-to-request-ready path.
    core_in.response.ready = 1'b0;
    send_core_request(ADDRESS);
    send_core_request(ADDRESS + 64'd4);
    repeat (4) tick();
    assert (core_out.response.valid && core_out.response.bits.word == 32'h11111111)
      else $fatal(1, "L1I did not preserve the first stalled response");
    core_in.request.bits.address = ADDRESS + 64'd8;
    core_in.request.valid = 1'b1;
    #1;
    assert (!core_out.request.ready)
      else $fatal(1, "L1I admitted a request without response capacity");
    core_in.response.ready = 1'b1;
    #1;
    assert (!core_out.request.ready)
      else $fatal(1, "L1I request ready depends combinationally on response ready");
    tick();
    assert (core_out.response.valid && core_out.response.bits.word == 32'h22222222)
      else $fatal(1, "L1I did not preserve the second stalled response");
    assert (core_out.request.ready)
      else $fatal(1, "L1I did not expose released response capacity");
    tick();
    core_in.request.valid = 1'b0;
    expect_instruction(32'h33333333);

    // SnpQuery reports the precise clean state without changing residency.
    grant_rsp_credit();
    send_snoop(ADDRESS, SNP_QUERY, 12'h066, 1'b0);
    expect_snoop_response(12'h066, 3'd1);
    send_core_request(ADDRESS);
    expect_instruction(32'h11111111);

    // A DVM operation comprises two packets and receives exactly one response.
    grant_rsp_credit();
    send_snoop(ADDRESS, SNP_DVM_OP, 12'h055, 1'b0);
    repeat (2) tick();
    assert (!chi_out.requester_responses.valid)
      else $fatal(1, "L1I responded to only half of a DVM operation");
    send_snoop(ADDRESS, SNP_DVM_OP, 12'h055, 1'b0);
    expect_snoop_response(12'h055, 3'd0);
    send_core_request(ADDRESS);
    expect_instruction(32'h11111111);

    // Without a snoop-data path, RetToSrc causes a legal silent clean
    // eviction before the cache reports Invalid.
    grant_rsp_credit();
    send_snoop(ADDRESS, SNP_SHARED, 12'h044, 1'b1);
    expect_snoop_response(12'h044, 3'd0);
    grant_req_credit();
    grant_rsp_credit();
    send_core_request(ADDRESS);
    accept_read_request(ADDRESS);
    return_line(ADDRESS, LINE);
    accept_comp_ack();
    expect_instruction(32'h11111111);

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
    core_in.flush = 1'b1;
    tick();
    core_in.flush = 1'b0;
    send_core_request(ADDRESS + 64'd4);
    expect_instruction(32'h22222222);
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

    // Two colliding lines coexist, and snoop lookup and invalidation select
    // the matching way without disturbing the other resident line.
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
    grant_rsp_credit();
    send_snoop(COLLIDE_B_ADDRESS, SNP_QUERY, 12'h088, 1'b0);
    expect_snoop_response(12'h088, 3'd1);
    grant_rsp_credit();
    invalidate_cache(COLLIDE_B_ADDRESS);
    send_core_request(ADDRESS);
    expect_instruction(32'h11111111);
    grant_req_credit();
    grant_rsp_credit();
    send_core_request(COLLIDE_B_ADDRESS);
    accept_read_request(COLLIDE_B_ADDRESS);
    return_line(COLLIDE_B_ADDRESS, LINE_B);
    accept_comp_ack();
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

    $display("RV5Stage VIPT instruction-cache simulation passed");
    $finish;
  end
endmodule
