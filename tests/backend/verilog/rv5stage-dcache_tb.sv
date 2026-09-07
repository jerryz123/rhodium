// Verifies RV5Stage VIPT L1D aliases, canceled reads, structural buffering, mutations, and coherence.
module rv5stage_dcache_tb;
  typedef struct packed {
    logic [63:0] address;
    logic [2:0] access;
    logic [3:0] atomic;
    logic [1:0] width;
    logic unsigned_load;
    logic [63:0] data;
    logic [1:0] destination;
    logic [4:0] rd;
    logic [1:0] floating_point_precision;
  } core_req_bits_t;
  typedef struct packed { logic valid; core_req_bits_t bits; } core_req_t;
  typedef struct packed { logic [63:0] address; logic [1:0] operation; } prefetch_bits_t;
  typedef struct packed { logic valid; prefetch_bits_t bits; } prefetch_t;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed {
    logic [63:0] data;
    logic [1:0] destination;
    logic [4:0] rd;
    logic [1:0] floating_point_precision;
  } core_resp_bits_t;
  typedef struct packed { logic valid; core_resp_bits_t bits; } core_resp_t;
  typedef struct packed { core_req_t request; } core_in_t;
  typedef struct packed { ready_t request; logic request_fault; logic request_access_fault; core_resp_t response; logic drained; } core_out_t;

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
  localparam logic [6:0] READ_UNIQUE = 7'h07;
  localparam logic [6:0] WRITE_UNIQUE_PTL = 7'h18;
  localparam logic [4:0] COMP_ACK = 5'h02;
  localparam logic [4:0] COMP = 5'h04;
  localparam logic [4:0] COMP_DBID_RESP = 5'h05;
  localparam logic [4:0] DBID_RESP_ORD = 5'h0e;
  localparam logic [4:0] RETRY_ACK = 5'h03;
  localparam logic [4:0] PCRD_GRANT = 5'h07;
  localparam logic [4:0] SNP_MAKE_INVALID = 5'h0a;
  localparam logic [3:0] SNP_RESP_DATA = 4'h1;
  localparam logic [3:0] NON_COPY_BACK_WRITE_DATA = 4'h3;
  localparam logic [3:0] COMP_DATA = 4'h4;
  localparam logic [6:0] HOME_ID = 7'd1;
  localparam logic [6:0] CACHE_ID = 7'd3;
  localparam logic [2:0] MEMORY_LOAD = 3'd1;
  localparam logic [2:0] MEMORY_STORE = 3'd2;
  localparam logic [2:0] MEMORY_LR = 3'd3;
  localparam logic [2:0] MEMORY_SC = 3'd4;
  localparam logic [2:0] MEMORY_ATOMIC = 3'd5;
  localparam logic [2:0] MEMORY_ZERO = 3'd6;
  localparam logic [3:0] ATOMIC_SWAP = 4'd0;
  localparam logic [3:0] ATOMIC_ADD = 4'd1;
  localparam logic [1:0] DATA_DESTINATION_NONE = 2'd0;
  localparam logic [1:0] DATA_DESTINATION_INTEGER = 2'd1;

  logic clock = 1'b0;
  logic reset = 1'b1;
  logic [6:0] node_id = CACHE_ID;
  core_in_t core_in;
  core_out_t core_out;
  prefetch_t prefetch_in;
  typedef struct packed { logic valid; logic [63:0] bits; } lookup_t;
  lookup_t virtual_lookup_in;
  logic probe_only = 1'b0;
  logic [63:0] virtual_page_xor = 64'h4000_0000;
  assign virtual_lookup_in = {core_in.request.valid | probe_only,
                              core_in.request.bits.address ^ virtual_page_xor};
  chi_in_t chi_in;
  chi_out_t chi_out;
  logic tx_req_pending = 1'b0;
  logic tx_rsp_pending = 1'b0;
  logic tx_dat_pending = 1'b0;
  logic forbid_core_response = 1'b0;
  CHIReqFlit captured_req;
  CHIRspFlit captured_rsp;
  CHIDatFlit captured_dat;

  RV5StageL1DCache dut (.*);
  always #5 clock = ~clock;

  task automatic tick;
    begin
      if (forbid_core_response)
        assert (!core_out.response.valid)
          else $fatal(1, "L1D produced a response for a prefetch");
      if (!reset && chi_out.requests.valid) begin
        tx_req_pending = 1'b1;
        captured_req = chi_out.requests.bits;
      end
      if (!reset && chi_out.requester_responses.valid) begin
        tx_rsp_pending = 1'b1;
        captured_rsp = chi_out.requester_responses.bits;
      end
      if (!reset && chi_out.request_data.valid) begin
        tx_dat_pending = 1'b1;
        captured_dat = chi_out.request_data.bits;
      end
      @(posedge clock);
      #1;
    end
  endtask

  task automatic send_prefetch(input logic [63:0] address,
                               input logic [1:0] operation);
    begin
      prefetch_in.bits.address = address;
      prefetch_in.bits.operation = operation;
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

  task automatic grant_dat_credit;
    begin
      chi_in.request_data.ready = 1'b1;
    end
  endtask

  task automatic wait_rsp_credit;
    integer cycles;
    begin
      cycles = 0;
      while (!chi_out.responses.ready && cycles < 50) begin
        tick();
        cycles = cycles + 1;
      end
      assert (chi_out.responses.ready)
        else $fatal(1, "L1D did not accept a response");
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
        else $fatal(1, "L1D did not accept response data");
    end
  endtask

  task automatic send_core_request(
    input logic [63:0] address,
    input logic [2:0] access,
    input logic [3:0] atomic,
    input logic [63:0] data,
    input logic [4:0] rd
  );
    integer cycles;
    begin
      cycles = 0;
      while (!core_out.request.ready && cycles < 100) begin
        tick();
        cycles = cycles + 1;
      end
      assert (core_out.request.ready)
        else $fatal(1, "L1D did not accept a core request");
      core_in.request.bits = '{address: address,
                               access: access,
                               atomic: atomic,
                               width: 2'd3,
                               unsigned_load: 1'b0,
                               data: data,
                               destination: (access == MEMORY_STORE || access == MEMORY_ZERO) ? DATA_DESTINATION_NONE : DATA_DESTINATION_INTEGER,
                               rd: rd,
                               floating_point_precision: 2'b01};
      core_in.request.valid = 1'b1;
      tick();
      core_in.request.valid = 1'b0;
    end
  endtask

  task automatic accept_request(
    input logic [6:0] opcode,
    input logic [63:0] address,
    input logic [11:0] txn_id,
    input logic [5:0] size,
    input logic allow_retry,
    input logic [3:0] pcrd_type
  );
    integer cycles;
    begin
      cycles = 0;
      while (!tx_req_pending && cycles < 100) begin
        tick();
        cycles = cycles + 1;
      end
      assert (tx_req_pending)
        else $fatal(1, "L1D did not issue a CHI request");
      assert (captured_req.opcode == opcode &&
              captured_req.src_id == CACHE_ID &&
              captured_req.tgt_id == HOME_ID &&
              captured_req.txn_id == txn_id &&
              captured_req.return_txn_id_or_stash_lpid == 12'd0 &&
              captured_req.address == address[43:0] &&
              captured_req.size_or_num_req == size &&
              captured_req.snp_attr_or_do_dwt == 1'b1 &&
              captured_req.mem_attr == (((opcode == READ_CLEAN) ||
                                         (opcode == READ_UNIQUE)) ? 4'hd : 4'h5) &&
              captured_req.exp_comp_ack == ((opcode == READ_CLEAN) ||
                                             (opcode == READ_UNIQUE)) &&
              captured_req.allow_retry == allow_retry &&
              captured_req.pcrd_type == pcrd_type)
        else $fatal(1, "L1D emitted malformed CHI request");
      tx_req_pending = 1'b0;
    end
  endtask

  task automatic return_line(
    input logic [63:0] address,
    input logic [511:0] line,
    input logic [2:0] response_state
  );
    integer packet;
    begin
      for (packet = 3; packet >= 0; packet = packet - 1) begin
        wait_dat_credit();
        chi_in.response_data.bits = '0;
        chi_in.response_data.bits.data = line[packet * 128 +: 128];
        chi_in.response_data.bits.byte_enable = 16'hffff;
        chi_in.response_data.bits.data_id = address[5:4] + packet[1:0];
        chi_in.response_data.bits.resp = response_state;
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
      while (!tx_rsp_pending && cycles < 100) begin
        tick();
        cycles = cycles + 1;
      end
      assert (tx_rsp_pending &&
              captured_rsp.opcode == COMP_ACK &&
              captured_rsp.txn_id == 12'h055)
        else $fatal(1, "L1D emitted malformed CompAck");
      tx_rsp_pending = 1'b0;
    end
  endtask

  task automatic expect_core_response(input logic [63:0] data,
                                      input logic [1:0] destination,
                                      input logic [4:0] rd);
    integer cycles;
    begin
      cycles = 0;
      while (!core_out.response.valid && cycles < 100) begin
        tick();
        cycles = cycles + 1;
      end
      assert (core_out.response.valid &&
              core_out.response.bits.data == data &&
              core_out.response.bits.destination == destination &&
              core_out.response.bits.rd == rd &&
              core_out.response.bits.floating_point_precision == 2'b01)
        else $fatal(1,
                    "L1D response mismatch: valid=%0d data=%h expected=%h destination=%0d expected_destination=%0d rd=%0d expected_rd=%0d",
                    core_out.response.valid,
                    core_out.response.bits.data,
                    data,
                    core_out.response.bits.destination,
                    destination,
                    core_out.response.bits.rd,
                    rd);
      tick();
    end
  endtask

  task automatic send_response(
    input logic [4:0] opcode,
    input logic [11:0] txn_id,
    input logic [11:0] dbid,
    input logic [3:0] pcrd_type
  );
    begin
      wait_rsp_credit();
      chi_in.responses.bits = '0;
      chi_in.responses.bits.dbid_or_group_id = dbid;
      chi_in.responses.bits.pcrd_type = pcrd_type;
      chi_in.responses.bits.opcode = opcode;
      chi_in.responses.bits.txn_id = txn_id;
      chi_in.responses.bits.src_id = HOME_ID;
      chi_in.responses.bits.tgt_id = CACHE_ID;
      chi_in.responses.valid = 1'b1;
      tick();
      chi_in.responses.valid = 1'b0;
      chi_in.responses.bits = '0;
    end
  endtask

  task automatic accept_write_data(
    input logic [63:0] data,
    input integer data_id,
    input logic high_lane
  );
    integer cycles;
    begin
      cycles = 0;
      while (!tx_dat_pending && cycles < 100) begin
        tick();
        cycles = cycles + 1;
      end
      assert (tx_dat_pending)
        else $fatal(1, "L1D did not issue write data");
      assert (captured_dat.opcode == NON_COPY_BACK_WRITE_DATA &&
              captured_dat.src_id == CACHE_ID &&
              captured_dat.tgt_id == HOME_ID &&
              captured_dat.txn_id == 12'h055 &&
              captured_dat.data_id == data_id[1:0] &&
              captured_dat.ccid == data_id[1:0] &&
              captured_dat.data[63:0] == (high_lane ? 64'd0 : data) &&
              captured_dat.data[127:64] == (high_lane ? data : 64'd0) &&
              captured_dat.byte_enable == (high_lane ? 16'hff00 : 16'h00ff))
        else $fatal(1, "L1D emitted malformed write data");
      tx_dat_pending = 1'b0;
    end
  endtask

  task automatic send_snoop(input logic [63:0] address,
                            input logic [11:0] txn_id);
    integer cycles;
    begin
      cycles = 0;
      while (!chi_out.snoops.ready && cycles < 100) begin
        tick();
        cycles = cycles + 1;
      end
      assert (chi_out.snoops.ready)
        else $fatal(1, "L1D did not accept a snoop");
      chi_in.snoops.bits = '0;
      chi_in.snoops.bits.address = address[43:3];
      chi_in.snoops.bits.opcode = SNP_MAKE_INVALID;
      chi_in.snoops.bits.txn_id = txn_id;
      chi_in.snoops.bits.src_id = HOME_ID;
      chi_in.snoops.valid = 1'b1;
      tick();
      chi_in.snoops = '0;
    end
  endtask

  task automatic accept_snoop_data(input integer packet,
                                   input logic [511:0] line,
                                   input logic [11:0] txn_id);
    integer cycles;
    begin
      chi_in.request_data.ready = 1'b0;
      cycles = 0;
      while (!chi_out.request_data.valid && cycles < 100) begin
        tick();
        cycles = cycles + 1;
      end
      assert (chi_out.request_data.valid &&
              chi_out.request_data.bits.opcode == SNP_RESP_DATA &&
              chi_out.request_data.bits.src_id == CACHE_ID &&
              chi_out.request_data.bits.tgt_id == HOME_ID &&
              chi_out.request_data.bits.txn_id == txn_id &&
              chi_out.request_data.bits.data_id == packet[1:0] &&
              chi_out.request_data.bits.resp == 3'b100 &&
              chi_out.request_data.bits.data == line[packet * 128 +: 128])
        else $fatal(1,
                    "dirty snoop DAT mismatch packet=%0d opcode=%h src=%0d tgt=%0d txn=%h id=%0d resp=%b data=%h expected=%h",
                    packet,
                    chi_out.request_data.bits.opcode,
                    chi_out.request_data.bits.src_id,
                    chi_out.request_data.bits.tgt_id,
                    chi_out.request_data.bits.txn_id,
                    chi_out.request_data.bits.data_id,
                    chi_out.request_data.bits.resp,
                    chi_out.request_data.bits.data,
                    line[packet * 128 +: 128]);
      chi_in.request_data.ready = 1'b1;
      tick();
      chi_in.request_data.ready = 1'b0;
      tx_dat_pending = 1'b0;
    end
  endtask

  localparam logic [63:0] ADDRESS = 64'h00000001_00000000;
  localparam logic [511:0] LINE = {
    64'h0f0e0d0c_0b0a0908,
    64'h07060504_03020100,
    64'hfedcba98_76543210,
    64'h11223344_55667788,
    64'hffeeddcc_bbaa9988,
    64'h77665544_33221100,
    64'h01234567_89abcdef,
    64'h88776655_44332211
  };
  localparam logic [63:0] STORE_DATA = 64'hdeadbeef_cafef00d;
  localparam logic [63:0] STORE_DATA_2 = 64'h01234567_89abcdef;
  localparam logic [63:0] EVICT_ADDRESS = ADDRESS + 64'h200;
  localparam logic [63:0] THIRD_ADDRESS = ADDRESS + 64'h400;
  localparam logic [63:0] PREFETCH_READ_ADDRESS = ADDRESS + 64'h40;
  localparam logic [63:0] PREFETCH_WRITE_ADDRESS = ADDRESS + 64'h80;
  localparam logic [511:0] EVICT_LINE = {
    64'h17161514_13121110,
    64'h0f0e0d0c_0b0a0908,
    64'hfffefdfc_fbfaf9f8,
    64'hf7f6f5f4_f3f2f1f0,
    64'h27262524_23222120,
    64'h1f1e1d1c_1b1a1918,
    64'h07060504_03020100,
    64'h37363534_33323130
  };
  localparam logic [511:0] THIRD_LINE = {EVICT_LINE[511:64], 64'habcdef01_23456789};
  logic [511:0] dirty_line;
  logic [511:0] evict_dirty_line;
  integer beat;

  initial begin
    core_in = '0;
    prefetch_in = '0;
    chi_in = '0;
    repeat (2) tick();
    reset = 1'b0;
    grant_req_credit();
    grant_rsp_credit();
    assert (core_out.drained)
      else $fatal(1, "data cache was not drained after reset");

    probe_only = 1'b1;
    core_in.request.bits.address = ADDRESS;
    repeat (4) begin
      tick();
      assert (!core_out.response.valid && !tx_req_pending && core_out.drained)
        else $fatal(1, "unresolved virtual lookup caused a response or refill");
    end
    probe_only = 1'b0;

    forbid_core_response = 1'b1;
    send_prefetch(PREFETCH_READ_ADDRESS, 2'd2);
    accept_request(READ_CLEAN, PREFETCH_READ_ADDRESS, 12'd0, 6'd6, 1'b1, 4'd0);
    return_line(PREFETCH_READ_ADDRESS, LINE, 3'b001);
    accept_comp_ack();
    wait (core_out.drained);
    tick();
    forbid_core_response = 1'b0;
    send_core_request(PREFETCH_READ_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd1);
    expect_core_response(64'h88776655_44332211, DATA_DESTINATION_INTEGER, 5'd1);

    virtual_page_xor = 64'h8000_0000;
    send_core_request(PREFETCH_READ_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd1);
    expect_core_response(64'h88776655_44332211, DATA_DESTINATION_INTEGER, 5'd1);
    send_core_request(PREFETCH_READ_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd2);
    send_core_request(PREFETCH_READ_ADDRESS + 64'd8, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd3);
    assert (core_out.response.valid)
      else $fatal(1, "VIPT load hit did not bypass the empty structural buffer");
    expect_core_response(64'h88776655_44332211, DATA_DESTINATION_INTEGER, 5'd2);
    assert (core_out.response.valid)
      else $fatal(1, "consecutive VIPT load hits inserted a response bubble");
    expect_core_response(64'h01234567_89abcdef, DATA_DESTINATION_INTEGER, 5'd3);

    forbid_core_response = 1'b1;
    send_prefetch(PREFETCH_WRITE_ADDRESS, 2'd3);
    accept_request(READ_UNIQUE, PREFETCH_WRITE_ADDRESS, 12'd0, 6'd6, 1'b1, 4'd0);
    return_line(PREFETCH_WRITE_ADDRESS, LINE, 3'b010);
    accept_comp_ack();
    wait (core_out.drained);
    tick();
    forbid_core_response = 1'b0;
    send_core_request(PREFETCH_WRITE_ADDRESS, MEMORY_STORE, ATOMIC_SWAP, STORE_DATA, 5'd0);
    expect_core_response(64'd0, DATA_DESTINATION_NONE, 5'd0);
    tick();
    assert (!tx_req_pending && !tx_dat_pending)
      else $fatal(1, "store after PREFETCH.W did not hit with Unique ownership");

    send_core_request(ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd3);
    assert (!core_out.drained)
      else $fatal(1, "data cache reported drained during a refill");
    // The first miss may stop internal queue drain, but its SRAM result must
    // not feed back into request acceptance while structural capacity remains.
    tick();
    assert (core_out.request.ready)
      else $fatal(1, "data cache request readiness depended on a lookup miss");
    send_core_request(ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd4);
    accept_request(READ_CLEAN, ADDRESS, 12'd0, 6'd6, 1'b1, 4'd0);
    send_response(PCRD_GRANT, 12'd0, 12'd0, 4'd6);
    send_response(RETRY_ACK, 12'd0, 12'd0, 4'd6);
    grant_req_credit();
    accept_request(READ_CLEAN, ADDRESS, 12'd0, 6'd6, 1'b0, 4'd6);
    return_line(ADDRESS, LINE, 3'b001);
    accept_comp_ack();
    expect_core_response(64'h88776655_44332211, DATA_DESTINATION_INTEGER, 5'd3);
    expect_core_response(64'h88776655_44332211, DATA_DESTINATION_INTEGER, 5'd4);

    // An AMO to a shared line acquires Unique ownership, returns the old
    // doubleword, installs the updated value, and leaves the line dirty.
    grant_req_credit();
    send_core_request(ADDRESS + 64'h18, MEMORY_ATOMIC, ATOMIC_ADD, 64'd1, 5'd7);
    assert (!core_out.drained)
      else $fatal(1, "data cache reported drained during ownership acquisition");
    accept_request(READ_UNIQUE, ADDRESS, 12'd0, 6'd6, 1'b1, 4'd0);
    return_line(ADDRESS, LINE, 3'b010);
    accept_comp_ack();
    expect_core_response(64'hffeeddcc_bbaa9988, DATA_DESTINATION_INTEGER, 5'd7);
    assert (core_out.drained)
      else $fatal(1, "data cache did not drain after ownership acquisition");
    assert (!tx_dat_pending)
      else $fatal(1, "write-allocate AMO unexpectedly emitted write data");

    send_core_request(ADDRESS + 64'h18, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd6);
    expect_core_response(64'hffeeddcc_bbaa9989, DATA_DESTINATION_INTEGER, 5'd6);

    // A second store hits UniqueDirty and remains entirely local.
    send_core_request(ADDRESS + 64'h28, MEMORY_STORE, ATOMIC_SWAP, STORE_DATA_2, 5'd0);
    assert (!core_out.response.valid)
      else $fatal(1, "local store responded in its SRAM lookup cycle");
    tick();
    assert (!core_out.response.valid)
      else $fatal(1, "local store bypassed the registered mutation stage");
    expect_core_response(64'd0, DATA_DESTINATION_NONE, 5'd0);
    tick();
    assert (!tx_req_pending && !tx_dat_pending)
      else $fatal(1, "UniqueDirty store unexpectedly reached CHI");
    assert (core_out.drained)
      else $fatal(1, "data cache did not drain after local dirty store");

    // LR observes the dirty line. Its matching SC succeeds once, returns zero,
    // and a second SC fails without issuing any coherence traffic.
    send_core_request(ADDRESS + 64'h28, MEMORY_LR, ATOMIC_SWAP, 64'd0, 5'd8);
    expect_core_response(STORE_DATA_2, DATA_DESTINATION_INTEGER, 5'd8);
    // A rejected SC may read the SRAM but cannot consume the LR reservation
    // or write data. The next permitted SC through another alias must succeed.
    core_in.request.bits.access = MEMORY_SC;
    core_in.request.bits.data = 64'hbad;
    probe_only = 1'b1;
    repeat (4) begin
      tick();
      assert (!core_out.response.valid && !tx_req_pending && !tx_dat_pending)
        else $fatal(1, "rejected SC produced a completion or coherence traffic");
    end
    probe_only = 1'b0;
    virtual_page_xor = 64'hc000_0000;
    send_core_request(ADDRESS + 64'h28, MEMORY_SC, ATOMIC_SWAP, STORE_DATA, 5'd9);
    expect_core_response(64'd0, DATA_DESTINATION_INTEGER, 5'd9);
    send_core_request(ADDRESS + 64'h28, MEMORY_SC, ATOMIC_SWAP, STORE_DATA_2, 5'd10);
    expect_core_response(64'd1, DATA_DESTINATION_INTEGER, 5'd10);
    tick();
    assert (!tx_req_pending && !tx_dat_pending)
      else $fatal(1, "failed SC unexpectedly reached CHI");
    send_core_request(ADDRESS + 64'h28, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd11);
    expect_core_response(STORE_DATA, DATA_DESTINATION_INTEGER, 5'd11);

    // A second colliding line occupies the invalid way without evicting the
    // dirty first line. A third collision then selects that round-robin victim
    // and drains all eight 64-bit beats before issuing its ReadClean.
    dirty_line = LINE;
    dirty_line[3 * 64 +: 64] = 64'hffeeddcc_bbaa9989;
    dirty_line[5 * 64 +: 64] = STORE_DATA;
    send_core_request(ADDRESS + 64'h28, MEMORY_LR, ATOMIC_SWAP, 64'd0, 5'd14);
    expect_core_response(STORE_DATA, DATA_DESTINATION_INTEGER, 5'd14);
    grant_req_credit();
    grant_rsp_credit();
    grant_dat_credit();
    send_core_request(EVICT_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd5);
    accept_request(READ_CLEAN, EVICT_ADDRESS, 12'd0, 6'd6, 1'b1, 4'd0);
    return_line(EVICT_ADDRESS, EVICT_LINE, 3'b001);
    accept_comp_ack();
    expect_core_response(64'h37363534_33323130, DATA_DESTINATION_INTEGER, 5'd5);
    // Filling an invalid colliding way does not replace the reserved line.
    send_core_request(ADDRESS + 64'h28, MEMORY_SC, ATOMIC_SWAP, STORE_DATA, 5'd16);
    expect_core_response(64'd0, DATA_DESTINATION_INTEGER, 5'd16);
    send_core_request(ADDRESS + 64'h28, MEMORY_LR, ATOMIC_SWAP, 64'd0, 5'd19);
    expect_core_response(STORE_DATA, DATA_DESTINATION_INTEGER, 5'd19);
    send_core_request(THIRD_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd17);
    for (beat = 0; beat < 8; beat = beat + 1) begin
      accept_request(WRITE_UNIQUE_PTL,
                     ADDRESS + beat * 8,
                     12'd1,
                     6'd3,
                     1'b1,
                     4'd0);
      send_response(COMP_DBID_RESP, 12'd1, 12'h055, 4'd0);
      accept_write_data(dirty_line[beat * 64 +: 64],
                        beat / 2,
                        (beat & 1) != 0);
    end
    accept_request(READ_CLEAN, THIRD_ADDRESS, 12'd0, 6'd6, 1'b1, 4'd0);
    return_line(THIRD_ADDRESS, THIRD_LINE, 3'b001);
    accept_comp_ack();
    expect_core_response(64'habcdef01_23456789, DATA_DESTINATION_INTEGER, 5'd17);
    send_core_request(ADDRESS + 64'h28, MEMORY_SC, ATOMIC_SWAP, STORE_DATA_2, 5'd15);
    expect_core_response(64'd1, DATA_DESTINATION_INTEGER, 5'd15);

    // Dirty snoop intervention returns the complete authoritative line and
    // invalidates the local copy without issuing a control-only SnpResp.
    send_core_request(EVICT_ADDRESS + 64'h8, MEMORY_STORE, ATOMIC_SWAP, STORE_DATA, 5'd0);
    accept_request(READ_UNIQUE, EVICT_ADDRESS, 12'd0, 6'd6, 1'b1, 4'd0);
    return_line(EVICT_ADDRESS, EVICT_LINE, 3'b010);
    accept_comp_ack();
    expect_core_response(64'd0, DATA_DESTINATION_NONE, 5'd0);
    send_core_request(EVICT_ADDRESS + 64'h8, MEMORY_LR, ATOMIC_SWAP, 64'd0, 5'd12);
    expect_core_response(STORE_DATA, DATA_DESTINATION_INTEGER, 5'd12);
    evict_dirty_line = EVICT_LINE;
    evict_dirty_line[1 * 64 +: 64] = STORE_DATA;
    chi_in.request_data.ready = 1'b0;
    send_snoop(EVICT_ADDRESS, 12'h077);
    for (beat = 0; beat < 4; beat = beat + 1)
      accept_snoop_data(beat, evict_dirty_line, 12'h077);
    tick();
    send_core_request(EVICT_ADDRESS + 64'h8, MEMORY_SC, ATOMIC_SWAP, STORE_DATA_2, 5'd13);
    expect_core_response(64'd1, DATA_DESTINATION_INTEGER, 5'd13);
    tick();
    assert (!tx_req_pending && !tx_dat_pending)
      else $fatal(1, "snoop-invalidated SC unexpectedly reached CHI");
    assert (core_out.drained)
      else $fatal(1, "data cache did not drain after dirty snoop response");
    send_core_request(THIRD_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd18);
    expect_core_response(64'habcdef01_23456789, DATA_DESTINATION_INTEGER, 5'd18);

    // A unique hit zeros all words, ignores the byte offset and clears LR.
    send_core_request(PREFETCH_WRITE_ADDRESS, MEMORY_LR, ATOMIC_SWAP, 64'd0, 5'd12);
    expect_core_response(STORE_DATA, DATA_DESTINATION_INTEGER, 5'd12);
    for (int offset = 0; offset < 64; offset++) begin
      send_core_request(PREFETCH_WRITE_ADDRESS + 64'(offset), MEMORY_ZERO, ATOMIC_SWAP, ~64'd0, 5'd0);
      expect_core_response(64'd0, DATA_DESTINATION_NONE, 5'd0);
      assert (!tx_req_pending) else $fatal(1, "unique zero issued CHI traffic");
      for (int word = 0; word < 8; word++) begin
        send_core_request(PREFETCH_WRITE_ADDRESS + 64'(word * 8), MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd1);
        expect_core_response(64'd0, DATA_DESTINATION_INTEGER, 5'd1);
      end
    end
    send_core_request(PREFETCH_WRITE_ADDRESS, MEMORY_SC, ATOMIC_SWAP, STORE_DATA, 5'd12);
    expect_core_response(64'd1, DATA_DESTINATION_INTEGER, 5'd12);
    send_core_request(THIRD_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd18);
    expect_core_response(64'habcdef01_23456789, DATA_DESTINATION_INTEGER, 5'd18);

    // A shared hit must acquire Unique before writing, then a coherent observer
    // receives all zeros. It cannot see a partially overwritten SRAM line.
    grant_req_credit();
    send_core_request(PREFETCH_READ_ADDRESS + 64'd63, MEMORY_ZERO, ATOMIC_SWAP, ~64'd0, 5'd0);
    accept_request(READ_UNIQUE, PREFETCH_READ_ADDRESS, 12'd0, 6'd6, 1'b1, 4'd0);
    assert (!core_out.drained && !core_out.response.valid)
      else $fatal(1, "zero completed before ownership");
    return_line(PREFETCH_READ_ADDRESS, LINE, 3'b010);
    accept_comp_ack();
    expect_core_response(64'd0, DATA_DESTINATION_NONE, 5'd0);
    send_snoop(PREFETCH_READ_ADDRESS, 12'h078);
    for (beat = 0; beat < 4; beat++)
      accept_snoop_data(beat, 512'd0, 12'h078);
    tick();

    // The invalidated line takes the same ownership/install path on a miss.
    grant_req_credit();
    send_core_request(PREFETCH_READ_ADDRESS + 64'd1, MEMORY_ZERO, ATOMIC_SWAP, ~64'd0, 5'd0);
    accept_request(READ_UNIQUE, PREFETCH_READ_ADDRESS, 12'd0, 6'd6, 1'b1, 4'd0);
    return_line(PREFETCH_READ_ADDRESS, LINE, 3'b010);
    accept_comp_ack();
    expect_core_response(64'd0, DATA_DESTINATION_NONE, 5'd0);
    for (int word = 0; word < 8; word++) begin
      send_core_request(PREFETCH_READ_ADDRESS + 64'(word * 8), MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd1);
      expect_core_response(64'd0, DATA_DESTINATION_INTEGER, 5'd1);
    end
    // A zero miss must first preserve the dirty victim. Its eight writes
    // carry the old zeroed line, then the new block acquires Unique ownership.
    grant_req_credit();
    grant_dat_credit();
    send_core_request(PREFETCH_WRITE_ADDRESS + 64'h100, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd1);
    accept_request(READ_CLEAN, PREFETCH_WRITE_ADDRESS + 64'h100, 12'd0, 6'd6, 1'b1, 4'd0);
    return_line(PREFETCH_WRITE_ADDRESS + 64'h100, LINE, 3'b001);
    accept_comp_ack();
    expect_core_response(LINE[63:0], DATA_DESTINATION_INTEGER, 5'd1);
    send_core_request(PREFETCH_WRITE_ADDRESS + 64'h201, MEMORY_ZERO, ATOMIC_SWAP, ~64'd0, 5'd0);
    for (beat = 0; beat < 8; beat++) begin
      accept_request(WRITE_UNIQUE_PTL, PREFETCH_WRITE_ADDRESS + 64'(beat * 8), 12'd1, 6'd3, 1'b1, 4'd0);
      send_response(COMP_DBID_RESP, 12'd1, 12'h055, 4'd0);
      accept_write_data(64'd0, beat / 2, (beat & 1) != 0);
    end
    accept_request(READ_UNIQUE, PREFETCH_WRITE_ADDRESS + 64'h200, 12'd0, 6'd6, 1'b1, 4'd0);
    // Home may need a snoop before returning the owned block. Serve it while
    // awaiting refill, rather than reserving SRAM throughout the transaction.
    send_snoop(PREFETCH_READ_ADDRESS, 12'h079);
    for (beat = 0; beat < 4; beat++)
      accept_snoop_data(beat, 512'd0, 12'h079);
    return_line(PREFETCH_WRITE_ADDRESS + 64'h200, LINE, 3'b010);
    accept_comp_ack();
    expect_core_response(64'd0, DATA_DESTINATION_NONE, 5'd0);
    send_snoop(PREFETCH_WRITE_ADDRESS + 64'h200, 12'h07a);
    for (beat = 0; beat < 4; beat++)
      accept_snoop_data(beat, 512'd0, 12'h07a);
    tick();

    // Identical virtual addresses resolving to two different physical pages
    // must match different tags, even though they select the same set.
    virtual_page_xor = 64'h4000_1000;
    send_core_request(ADDRESS + 64'h10c0, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd20);
    accept_request(READ_CLEAN, ADDRESS + 64'h10c0, 12'd0, 6'd6, 1'b1, 4'd0);
    return_line(ADDRESS + 64'h10c0, LINE, 3'b001);
    accept_comp_ack();
    expect_core_response(64'h88776655_44332211, DATA_DESTINATION_INTEGER, 5'd20);
    virtual_page_xor = 64'h4000_2000;
    send_core_request(ADDRESS + 64'h20c0, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd21);
    accept_request(READ_CLEAN, ADDRESS + 64'h20c0, 12'd0, 6'd6, 1'b1, 4'd0);
    return_line(ADDRESS + 64'h20c0, THIRD_LINE, 3'b001);
    accept_comp_ack();
    expect_core_response(64'habcdef01_23456789, DATA_DESTINATION_INTEGER, 5'd21);
    virtual_page_xor = 64'h4000_1000;
    send_core_request(ADDRESS + 64'h10c0, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd22);
    expect_core_response(64'h88776655_44332211, DATA_DESTINATION_INTEGER, 5'd22);

    $display("RV5Stage VIPT write-back data-cache simulation passed");
    $finish;
  end
endmodule
