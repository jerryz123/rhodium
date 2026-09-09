// Verifies flow admission, set-isolated hits under a miss, stores, coherence, and atomics.
module rv5stage_dcache_tb;
  `include "tests/backend/verilog/rv5stage-amo-reference.svh"
  typedef struct packed {
    logic [63:0] address;
    logic [3:0] access;
    logic [3:0] atomic;
    logic [1:0] width;
    logic unsigned_load;
    logic [63:0] data;
    logic [1:0] destination;
    logic [4:0] rd;
    logic [1:0] floating_point_precision;
    logic [2:0] locality;
  } core_req_bits_t;
  typedef struct packed { logic valid; core_req_bits_t bits; } core_req_t;
  typedef struct packed { logic [63:0] address; logic [1:0] operation; } prefetch_bits_t;
  typedef struct packed { logic valid; prefetch_bits_t bits; } prefetch_t;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed {
    logic access_fault;
    logic [63:0] data;
    logic [1:0] destination;
    logic [4:0] rd;
    logic [1:0] floating_point_precision;
  } core_resp_bits_t;
  typedef struct packed { logic valid; core_resp_bits_t bits; } core_resp_t;
  typedef struct packed { core_req_t request; } core_in_t;
  typedef struct packed { ready_t request; logic request_fault; logic request_access_fault; core_resp_t response; logic drained; logic reservation_valid; } core_out_t;

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
  localparam logic [6:0] WRITE_BACK_FULL = 7'h1b;
  localparam logic [4:0] COMP_ACK = 5'h02;
  localparam logic [4:0] COMP = 5'h04;
  localparam logic [4:0] COMP_DBID_RESP = 5'h05;
  localparam logic [4:0] DBID_RESP_ORD = 5'h0e;
  localparam logic [4:0] RETRY_ACK = 5'h03;
  localparam logic [4:0] PCRD_GRANT = 5'h07;
  localparam logic [4:0] SNP_CLEAN_INVALID = 5'h09;
  localparam logic [3:0] SNP_RESP_DATA = 4'h1;
  localparam logic [3:0] COPY_BACK_WRITE_DATA = 4'h2;
  localparam logic [3:0] COMP_DATA = 4'h4;
  localparam logic [6:0] HOME_ID = 7'd1;
  localparam logic [6:0] CACHE_ID = 7'd3;
  localparam logic [3:0] MEMORY_LOAD = 4'd1;
  localparam logic [3:0] MEMORY_STORE = 4'd2;
  localparam logic [3:0] MEMORY_LR = 4'd3;
  localparam logic [3:0] MEMORY_SC = 4'd4;
  localparam logic [3:0] MEMORY_ATOMIC = 4'd5;
  localparam logic [3:0] MEMORY_ZERO = 4'd6;
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
  `include "tests/backend/verilog/rv5stage-pipeline-types.svh"
  pipeline_request_t pipeline_lookup_in='0;
  pipeline_in_t pipeline_in='0;
  pipeline_out_t pipeline_out;
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
  logic watch_amo_response = 0;
  integer amo_response_count = 0;
  logic watch_progress_snoop = 0;
  logic forbid_progress_snoop = 0;
  integer progress_snoop_accepts = 0;
  CHIReqFlit captured_req;
  CHIRspFlit captured_rsp;
  CHIDatFlit captured_dat;
  CHISnpFlit captured_snoop = '0;

  RV5StageL1DCache dut (.*);
  always #5 clock = ~clock;
  initial begin
    #1000000;
    $fatal(1, "L1D staged lookup/coherence watchdog expired");
  end

  task automatic tick;
    begin
      if (!reset && chi_in.snoops.valid && chi_out.snoops.ready)
        captured_snoop = chi_in.snoops.bits;
      if (forbid_progress_snoop)
        assert (!(chi_in.snoops.valid && chi_out.snoops.ready))
          else $fatal(1, "probe revoked the protected LR/SC ownership window");
      if (watch_progress_snoop && chi_in.snoops.valid && chi_out.snoops.ready)
        progress_snoop_accepts++;
      if (watch_amo_response && core_out.response.valid) begin
        assert (!core_out.response.bits.access_fault && core_out.response.bits.data == 1 && core_out.response.bits.rd == 2)
          else $fatal(1, "contended AMO lost its old-value response");
        amo_response_count++;
      end
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
      if (watch_progress_snoop && progress_snoop_accepts != 0)
        chi_in.snoops = '0;
    end
  endtask

  task automatic stage_pipeline_store(input logic [63:0] address, value, input logic [1:0] size=3);
    pipeline_in='0;
    pipeline_lookup_in='{valid:1,bits:'{address:address ^ virtual_page_xor,access:MEMORY_STORE,width:size,unsigned_0:0,data:value}};
    tick();
    pipeline_lookup_in='0;
    pipeline_in.request='{valid:1,bits:'{address:address,access:MEMORY_STORE,width:size,unsigned_0:0,data:value}};
    #1;
    assert(pipeline_out.response.valid && pipeline_out.response.bits.outcome==PIPE_STORE_HIT)
      else $fatal(1,"owned store did not resolve in MEM: %0d",pipeline_out.response.bits.outcome);
  endtask

  // Exercise a younger read at the WB enqueue boundary. Keep an EX read
  // presented so the buffer cannot disappear into an otherwise idle slot.
  task automatic store_then_load(input logic [63:0] store_address, value, load_address,
                                input logic [1:0] store_size, load_size,
                                input bit overlap,
                                input logic [63:0] expected);
    stage_pipeline_store(store_address,value,store_size);
    pipeline_lookup_in='{valid:1,bits:'{address:load_address ^ virtual_page_xor,access:MEMORY_LOAD,width:load_size,unsigned_0:1,data:0}};
    tick();
    pipeline_in.request='{valid:1,bits:'{address:load_address,access:MEMORY_LOAD,width:load_size,unsigned_0:1,data:0}};
    pipeline_in.commit=1;
    #1;
    assert(pipeline_out.commit_ready && !core_out.drained) else $fatal(1,"WB store authorization/drain boundary");
    assert(pipeline_out.response.bits.outcome==(overlap ? PIPE_REPLAY : PIPE_LOAD_HIT))
      else $fatal(1,"same-cycle physical-byte hazard mismatch: %0d",pipeline_out.response.bits.outcome);
    if(!overlap) assert(pipeline_out.response.bits.data==expected) else $fatal(1,"independent load data");
    tick();
    pipeline_in='0;
    pipeline_lookup_in='0;
    repeat(3) tick();
    assert(core_out.drained && !core_out.response.valid && !tx_req_pending) else $fatal(1,"committed store duplicated a response or transaction");
  endtask

  task automatic check_pipeline_load(input logic [63:0] address,
                                      input bit permitted, expected_hit,
                                      input logic [63:0] value=0);
    pipeline_lookup_in='{valid:1'b1,bits:'{address:address ^ virtual_page_xor,access:MEMORY_LOAD,width:2'd3,unsigned_0:1'b0,data:'0}};
    tick();
    pipeline_lookup_in='0;
    pipeline_in.request='{valid:permitted,bits:'{address:address,access:MEMORY_LOAD,width:2'd3,unsigned_0:1'b0,data:'0}};
    #1;
    assert((pipeline_out.response.valid && pipeline_out.response.bits.outcome==PIPE_LOAD_HIT)==expected_hit)
      else $fatal(1,"speculative MEM hit mismatch at %h",address);
    if(expected_hit) assert(pipeline_out.response.bits.data==value)
      else $fatal(1,"speculative MEM result mismatch");
    tick();
    pipeline_in='0;
    tick();
    assert(!core_out.response.valid && !tx_req_pending)
      else $fatal(1,"speculative lookup created an authorized response or transaction");
  endtask

  task automatic check_under_miss(input logic [63:0] address,
                                 input logic [2:0] outcome,
                                 input logic [63:0] value=0,
                                 input logic [3:0] operation=MEMORY_LOAD);
    pipeline_lookup_in='{valid:1,bits:'{address:address ^ virtual_page_xor,access:operation,width:3,unsigned_0:0,data:0}};
    tick();
    pipeline_lookup_in='0;
    pipeline_in.request='{valid:1,bits:'{address:address,access:operation,width:3,unsigned_0:0,data:0}};
    #1;
    assert(pipeline_out.response.valid && pipeline_out.response.bits.outcome==outcome)
      else $fatal(1,"hit-under-miss outcome at %h: got %0d expected %0d",address,pipeline_out.response.bits.outcome,outcome);
    if(outcome==PIPE_LOAD_HIT) assert(pipeline_out.response.bits.data==value)
      else $fatal(1,"hit-under-miss read stale/wrong data");
    assert(!core_out.drained && !core_out.response.valid && !tx_req_pending)
      else $fatal(1,"hit-under-miss altered the outstanding transaction");
    tick(); pipeline_in='0; tick();
  endtask

  task automatic stream_under_miss;
    pipeline_lookup_in='{valid:1,bits:'{address:PREFETCH_READ_ADDRESS ^ virtual_page_xor,access:MEMORY_LOAD,width:3,unsigned_0:0,data:0}};
    tick();
    pipeline_in.request='{valid:1,bits:'{address:PREFETCH_READ_ADDRESS,access:MEMORY_LOAD,width:3,unsigned_0:0,data:0}};
    repeat(12) begin
      #1;
      assert(pipeline_out.response.valid && pipeline_out.response.bits.outcome==PIPE_LOAD_HIT && pipeline_out.response.bits.data==LINE[63:0])
        else $fatal(1,"independent streaming loads stalled during CHI wait");
      assert(!core_out.drained && !core_out.response.valid && !tx_req_pending)
        else $fatal(1,"streaming hits created another transaction");
      tick();
    end
    pipeline_lookup_in='0; pipeline_in='0;
  endtask

  task automatic prepare_hit_under_miss;
    reset=1; core_in='0; pipeline_in='0; pipeline_lookup_in='0; chi_in='0; prefetch_in='0;
    tx_req_pending=0; tx_rsp_pending=0; tx_dat_pending=0;
    repeat(2) tick(); reset=0; grant_req_credit(); grant_rsp_credit(); grant_dat_credit();
    // Two ways in set zero plus an independent warm line in set one.
    send_core_request(ADDRESS,MEMORY_LOAD,ATOMIC_SWAP,0,1);
    accept_request(READ_CLEAN,ADDRESS,0,6,1,0);
    return_line(ADDRESS,LINE,3'b010); accept_comp_ack();
    expect_core_response(LINE[63:0],DATA_DESTINATION_INTEGER,1);
    send_core_request(EVICT_ADDRESS,MEMORY_LOAD,ATOMIC_SWAP,0,1);
    accept_request(READ_CLEAN,EVICT_ADDRESS,0,6,1,0);
    return_line(EVICT_ADDRESS,EVICT_LINE,3'b010); accept_comp_ack();
    expect_core_response(EVICT_LINE[63:0],DATA_DESTINATION_INTEGER,1);
    send_core_request(PREFETCH_READ_ADDRESS,MEMORY_LOAD,ATOMIC_SWAP,0,1);
    accept_request(READ_CLEAN,PREFETCH_READ_ADDRESS,0,6,1,0);
    return_line(PREFETCH_READ_ADDRESS,LINE,3'b010); accept_comp_ack();
    expect_core_response(LINE[63:0],DATA_DESTINATION_INTEGER,1);
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
    input logic [3:0] access,
    input logic [3:0] atomic,
    input logic [63:0] data,
    input logic [4:0] rd,
    input logic [2:0] locality = 3'd0,
    input logic [1:0] destination = 2'b11,
    input logic [1:0] size = 2'd3
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
                               width: size,
                               unsigned_load: 1'b0,
                               data: data,
                               destination: destination != 2'b11 ? destination : ((access == MEMORY_STORE || access == MEMORY_ZERO || access >= 7) ? DATA_DESTINATION_NONE : DATA_DESTINATION_INTEGER),
                               rd: rd,
                               floating_point_precision: 2'b01, locality: locality};
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
                                         (opcode == READ_UNIQUE) || (opcode == WRITE_BACK_FULL)) ? 4'hd : 4'h5) &&
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
    input logic [2:0] response_state,
    input bit exercise_hits = 0
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
        if(exercise_hits && packet!=0) begin
          check_under_miss(PREFETCH_READ_ADDRESS,PIPE_LOAD_HIT,LINE[63:0]);
          check_under_miss(address,PIPE_REPLAY); // No partial line may be observed.
        end
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
                                      input logic [4:0] rd,
                                      input logic access_fault = 0);
    integer cycles;
    begin
      cycles = 0;
      while (!core_out.response.valid && cycles < 100) begin
        tick();
        cycles = cycles + 1;
      end
      assert (core_out.response.valid &&
              core_out.response.bits.access_fault == access_fault &&
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
    input logic [3:0] pcrd_type,
    input logic [1:0] error = 0
  );
    begin
      wait_rsp_credit();
      chi_in.responses.bits = '0;
      chi_in.responses.bits.dbid_or_group_id = dbid;
      chi_in.responses.bits.pcrd_type = pcrd_type;
      chi_in.responses.bits.resp_err = error;
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

  task automatic accept_copyback_data(input integer packet,
                                     input logic [511:0] line,
                                     input logic [2:0] response = 3'b110);
    integer cycles;
    CHIDatFlit expected;
    begin
      cycles = 0;
      while (!tx_dat_pending && cycles < 100) begin
        tick();
        cycles++;
      end
      expected = '0;
      expected.opcode = COPY_BACK_WRITE_DATA;
      expected.src_id = CACHE_ID;
      expected.tgt_id = HOME_ID;
      expected.txn_id = 12'h055;
      expected.home_nid_or_pbha_or_mismatched_mecid = HOME_ID;
      expected.data_id = packet[1:0];
      expected.resp = response;
      expected.data = response == 0 ? 128'd0 : line[packet * 128 +: 128];
      expected.byte_enable = response == 0 ? 16'd0 : 16'hffff;
      assert (tx_dat_pending && captured_dat == expected)
        else $fatal(1, "L1D copyback packet %0d mismatch: got=%h expected=%h", packet, captured_dat, expected);
      tx_dat_pending = 1'b0;
    end
  endtask

  task automatic send_snoop(input logic [63:0] address,
                            input logic [11:0] txn_id,
                            input logic [4:0] opcode = SNP_CLEAN_INVALID);
    integer cycles;
    begin
      chi_in.snoops.bits = '0;
      chi_in.snoops.bits.address = address[43:3];
      chi_in.snoops.bits.opcode = opcode;
      chi_in.snoops.bits.txn_id = txn_id;
      chi_in.snoops.bits.src_id = HOME_ID;
      chi_in.snoops.bits.trace_tag = txn_id[0];
      chi_in.snoops.bits.qos = txn_id[3:0];
      chi_in.snoops.valid = 1'b1;
      // Present the real opcode while waiting; idle LCrdReturn is always ready.
      #1;
      cycles = 0;
      while (!chi_out.snoops.ready && cycles < 256) begin
        tick();
        cycles = cycles + 1;
      end
      assert (chi_out.snoops.ready)
        else $fatal(1, "L1D did not accept a snoop");
      tick();
      chi_in.snoops = '0;
    end
  endtask

  task automatic accept_snoop_data(input integer packet,
                                   input logic [511:0] line,
                                   input logic [11:0] txn_id);
    integer cycles;
    CHIDatFlit expected;
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
      expected = '0;
      expected.data = line[packet * 128 +: 128];
      expected.byte_enable = '1;
      expected.trace_tag = captured_snoop.trace_tag;
      expected.data_id = packet[1:0];
      expected.ccid = packet[1:0];
      expected.resp = 3'b100;
      expected.opcode = SNP_RESP_DATA;
      expected.home_nid_or_pbha_or_mismatched_mecid = HOME_ID;
      expected.txn_id = txn_id;
      expected.src_id = CACHE_ID;
      expected.tgt_id = HOME_ID;
      expected.qos = captured_snoop.qos;
      repeat (3) begin
        assert (chi_out.request_data.valid && chi_out.request_data.bits === expected)
          else $fatal(1, "complete dirty snoop DAT mismatch under stall: actual=%h expected=%h", chi_out.request_data.bits, expected);
        tick();
      end
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
    assert (!core_out.response.valid)
      else $fatal(1, "load response bypassed the registered S4 lookup result");
    tick();
    assert (core_out.response.valid)
      else $fatal(1, "VIPT load hit did not bypass the empty structural buffer");
    expect_core_response(64'h88776655_44332211, DATA_DESTINATION_INTEGER, 5'd2);
    assert (core_out.response.valid)
      else $fatal(1, "consecutive VIPT load hits inserted a response bubble");
    expect_core_response(64'h01234567_89abcdef, DATA_DESTINATION_INTEGER, 5'd3);

    check_pipeline_load(PREFETCH_READ_ADDRESS,1,1,64'h88776655_44332211);
    check_pipeline_load(PREFETCH_READ_ADDRESS+8,1,1,64'h01234567_89abcdef);
    check_pipeline_load(PREFETCH_READ_ADDRESS,0,0); // no permitted translation
    check_pipeline_load(ADDRESS,1,0); // cold lookup must not allocate

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
    assert (core_out.request.ready)
      else $fatal(1, "data cache request readiness depended on a lookup miss");
    // This younger lookup is already in S3 when the older S4 miss blocks it.
    // It must reread the installed line instead of launching a duplicate miss.
    send_core_request(ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd4);
    send_core_request(ADDRESS + 64'd8, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd5);
    accept_request(READ_CLEAN, ADDRESS, 12'd0, 6'd6, 1'b1, 4'd0);
    send_response(PCRD_GRANT, 12'd0, 12'd0, 4'd6);
    send_response(RETRY_ACK, 12'd0, 12'd0, 4'd6);
    grant_req_credit();
    accept_request(READ_CLEAN, ADDRESS, 12'd0, 6'd6, 1'b0, 4'd6);
    return_line(ADDRESS, LINE, 3'b001);
    accept_comp_ack();
    expect_core_response(64'h88776655_44332211, DATA_DESTINATION_INTEGER, 5'd3);
    expect_core_response(64'h88776655_44332211, DATA_DESTINATION_INTEGER, 5'd4);
    expect_core_response(64'h01234567_89abcdef, DATA_DESTINATION_INTEGER, 5'd5);
    assert (!tx_req_pending)
      else $fatal(1, "retained lookup used stale metadata after refill");

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
    // The immediately following load initially reads the pre-store SRAM word.
    // Its retained request must reread after the older mutation commits.
    send_core_request(ADDRESS + 64'h28, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd6);
    assert (!core_out.response.valid)
      else $fatal(1, "local store bypassed the registered ownership decision");
    tick();
    assert (!core_out.response.valid)
      else $fatal(1, "local store bypassed the registered mutation stage");
    expect_core_response(64'd0, DATA_DESTINATION_NONE, 5'd0);
    expect_core_response(STORE_DATA_2, DATA_DESTINATION_INTEGER, 5'd6);
    tick();
    assert (!tx_req_pending && !tx_dat_pending)
      else $fatal(1, "UniqueDirty store unexpectedly reached CHI");
    assert (core_out.drained)
      else $fatal(1, "data cache did not drain after local dirty store");

    // LR observes the dirty line. Its matching SC succeeds once, returns zero,
    // and a second SC fails without issuing any coherence traffic.
    send_core_request(ADDRESS + 64'h28, MEMORY_LR, ATOMIC_SWAP, 64'd0, 5'd8);
    expect_core_response(STORE_DATA_2, DATA_DESTINATION_INTEGER, 5'd8);
    assert (core_out.reservation_valid) else $fatal(1, "LR did not publish reservation status");
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
    assert (!core_out.reservation_valid) else $fatal(1, "SC did not clear reservation status");
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
    // Both colliding ways are resident, one dirty and reserved. Every NTL
    // selector reads the third line coherently without replacing either way.
    // Repeating that miss proves the transient copy was never installed.
    // An odd number of misses also detects accidental movement of the two-way
    // replacement pointer when the later default miss chooses its victim.
    for (int attempt = 0; attempt < 5; attempt++) begin
      automatic logic [2:0] locality = 3'(1 + attempt % 4);
      send_core_request(THIRD_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 5'd23, 3'(locality), 2'd2);
      if (attempt == 0)
        send_core_request(EVICT_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 5'd26);
      accept_request(READ_CLEAN, THIRD_ADDRESS, 0, 6, 1, 0);
      if (attempt == 0) begin
        send_response(RETRY_ACK, 0, 0, 4'd6);
        send_response(PCRD_GRANT, 0, 0, 4'd6);
        accept_request(READ_CLEAN, THIRD_ADDRESS, 0, 6, 0, 4'd6);
        // Snoop service must remain live while the transaction owns a retained
        // younger lookup, and the bypass address is not a resident copy.
        send_snoop(THIRD_ADDRESS, 12'h07c);
        for (int cycle = 0; !tx_rsp_pending && cycle < 100; cycle++) tick();
        assert (tx_rsp_pending && captured_rsp.opcode == 1 && captured_rsp.resp == 0 && captured_rsp.txn_id == 12'h07c)
          else $fatal(1, "NTL refill blocked snoop service or exposed a cached copy");
        tx_rsp_pending = 0;
      end
      chi_in.requester_responses.ready = 0;
      return_line(THIRD_ADDRESS, THIRD_LINE, locality[0] ? 3'b001 : 3'b010);
      repeat (5) begin
        assert (!core_out.response.valid && !core_out.drained && !tx_dat_pending)
          else $fatal(1, "NTL load completed before CompAck or wrote back a victim");
        tick();
      end
      assert (chi_out.requester_responses.valid && chi_out.requester_responses.bits.opcode == COMP_ACK)
        else $fatal(1, "NTL CompAck not retained under backpressure");
      grant_rsp_credit();
      tick();
      accept_comp_ack();
      expect_core_response(64'habcdef01_23456789, 2'd2, 5'd23);
      if (attempt == 0)
        expect_core_response(64'h37363534_33323130, DATA_DESTINATION_INTEGER, 5'd26);
      // Reservation lifetime is bounded independently of these deliberately
      // stalled transactions; residency is checked by the following hits.
      // Hinted dirty hits must read the local authoritative value, not memory.
      send_core_request(ADDRESS + 64'h28, MEMORY_LOAD, ATOMIC_SWAP, 0, 5'd24, 3'(locality));
      expect_core_response(STORE_DATA, DATA_DESTINATION_INTEGER, 5'd24);
      send_core_request(EVICT_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 5'd25);
      expect_core_response(64'h37363534_33323130, DATA_DESTINATION_INTEGER, 5'd25);
      assert (!tx_req_pending && !tx_dat_pending)
        else $fatal(1, "NTL miss changed a resident line");
    end
    // Default policy still chooses the original dirty round-robin victim.
    send_core_request(THIRD_ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd17);
    accept_request(WRITE_BACK_FULL, ADDRESS, 12'd1, 6'd6, 1'b1, 4'd0);
    send_response(COMP_DBID_RESP, 12'd1, 12'h055, 4'd0);
    for (beat = 0; beat < 4; beat++)
      accept_copyback_data(beat, dirty_line);
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
    check_pipeline_load(EVICT_ADDRESS+8,1,0); // snoop owns the arrays
    for (beat = 0; beat < 4; beat = beat + 1)
      accept_snoop_data(beat, evict_dirty_line, 12'h077);
    tick();
    assert (!core_out.reservation_valid) else $fatal(1, "snoop did not clear reservation status");
    send_core_request(EVICT_ADDRESS + 64'h8, MEMORY_SC, ATOMIC_SWAP, STORE_DATA_2, 5'd13);
    expect_core_response(64'd1, DATA_DESTINATION_INTEGER, 5'd13);
    tick();
    assert (!tx_req_pending && !tx_dat_pending)
      else $fatal(1, "snoop-invalidated SC unexpectedly reached CHI");
    assert (core_out.drained)
      else $fatal(1, "data cache did not drain after dirty snoop response");
    check_pipeline_load(EVICT_ADDRESS+8,1,0); // invalidation cannot expose stale hit data
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
    // A zero miss must first preserve the dirty victim. One copyback
    // carries the old zeroed line, then the new block acquires Unique ownership.
    grant_req_credit();
    grant_dat_credit();
    send_core_request(PREFETCH_WRITE_ADDRESS + 64'h100, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd1);
    accept_request(READ_CLEAN, PREFETCH_WRITE_ADDRESS + 64'h100, 12'd0, 6'd6, 1'b1, 4'd0);
    return_line(PREFETCH_WRITE_ADDRESS + 64'h100, LINE, 3'b001);
    accept_comp_ack();
    expect_core_response(LINE[63:0], DATA_DESTINATION_INTEGER, 5'd1);
    send_core_request(PREFETCH_WRITE_ADDRESS + 64'h201, MEMORY_ZERO, ATOMIC_SWAP, ~64'd0, 5'd0);
    send_core_request(PREFETCH_WRITE_ADDRESS + 64'h208, MEMORY_LOAD, ATOMIC_SWAP, 64'd0, 5'd2);
    accept_request(WRITE_BACK_FULL, PREFETCH_WRITE_ADDRESS, 12'd1, 6'd6, 1'b1, 4'd0);
    send_response(COMP_DBID_RESP, 12'd1, 12'h055, 4'd0);
    for (beat = 0; beat < 4; beat++)
      accept_copyback_data(beat, 512'd0);
    accept_request(READ_UNIQUE, PREFETCH_WRITE_ADDRESS + 64'h200, 12'd0, 6'd6, 1'b1, 4'd0);
    // Home may need a snoop before returning the owned block. Serve it while
    // awaiting refill, rather than reserving SRAM throughout the transaction.
    send_snoop(PREFETCH_READ_ADDRESS, 12'h079);
    for (beat = 0; beat < 4; beat++)
      accept_snoop_data(beat, 512'd0, 12'h079);
    return_line(PREFETCH_WRITE_ADDRESS + 64'h200, LINE, 3'b010);
    accept_comp_ack();
    expect_core_response(64'd0, DATA_DESTINATION_NONE, 5'd0);
    expect_core_response(64'd0, DATA_DESTINATION_INTEGER, 5'd2);
    // MakeInvalid discards even a dirty line and must return only SnpResp_I.
    send_snoop(PREFETCH_WRITE_ADDRESS + 64'h200, 12'h07a, 5'h0a);
    for (integer wait_cycles = 0; !chi_out.requester_responses.valid && wait_cycles < 100; wait_cycles++) begin
      assert(!chi_out.request_data.valid) else $fatal(1, "discard returned dirty data");
      tick();
    end
    assert(chi_out.requester_responses.valid && chi_out.requester_responses.bits.opcode == 1 && chi_out.requester_responses.bits.resp == 0 && chi_out.requester_responses.bits.txn_id == 12'h07a)
      else $fatal(1, "discard response mismatch");
    chi_in.requester_responses.ready = 1;
    tick();
    tx_rsp_pending = 1'b0;
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

    // A maintenance requester keeps snoop service live until Home completion.
    // Dirty data is preserved by clean/flush and deliberately discarded by inval.
    for (int operation = 7; operation <= 9; operation++) begin
      reset = 1; tick(); tick(); reset = 0;
      tx_req_pending = 0; tx_rsp_pending = 0; tx_dat_pending = 0;
      // NTL stores still acquire and install a dirty copy.
      send_core_request(ADDRESS, MEMORY_STORE, ATOMIC_SWAP, STORE_DATA, 0, 3'd4);
      accept_request(READ_UNIQUE, ADDRESS, 0, 6, 1, 0);
      return_line(ADDRESS, LINE, 3'b010);
      accept_comp_ack();
      expect_core_response(0, DATA_DESTINATION_NONE, 0);
      evict_dirty_line = LINE; evict_dirty_line[63:0] = STORE_DATA;
      send_core_request(ADDRESS + 63, 4'(operation), ATOMIC_SWAP, 0, 0);
      for (int cycles = 0; !tx_req_pending && cycles < 100; cycles++) tick();
      assert (tx_req_pending && captured_req.opcode == (operation == 7 ? 7'h0a : operation == 8 ? 7'h08 : 7'h09) && captured_req.address == ADDRESS[43:0] && captured_req.txn_id == 2 && captured_req.excl_snoop_me_cah && captured_req.mem_attr == 4'h4 && !captured_req.exp_comp_ack)
        else $fatal(1, "bad maintenance command");
      tx_req_pending = 0;
      if (operation == 8) begin
        send_response(PCRD_GRANT, 0, 0, 4'h5);
        send_response(RETRY_ACK, 2, 0, 4'h5);
        for (int cycles = 0; !tx_req_pending && cycles < 100; cycles++) tick();
        assert (tx_req_pending && captured_req.opcode == 7'h08 && !captured_req.allow_retry && captured_req.pcrd_type == 5 && captured_req.txn_id == 2)
          else $fatal(1, "CMO retry lost command or credit");
        tx_req_pending = 0;
      end
      chi_in.request_data.ready = 0;
      send_snoop(ADDRESS, 12'h07b, operation == 7 ? 5'h0a : operation == 8 ? 5'h08 : 5'h09);
      if (operation == 7) begin
        for (int cycles = 0; !chi_out.requester_responses.valid && cycles < 100; cycles++) begin
          assert (!chi_out.request_data.valid) else $fatal(1, "CMO invalidate wrote dirty data");
          tick();
        end
        assert (chi_out.requester_responses.valid && chi_out.requester_responses.bits.resp == 0)
          else $fatal(1, "CMO invalidate did not respond");
        tick(); tx_rsp_pending = 0;
      end else begin
        for (beat = 0; beat < 4; beat++)
          accept_snoop_data(beat, evict_dirty_line, 12'h07b);
        tick();
      end
      repeat (5) begin
        assert (!core_out.response.valid && !core_out.drained) else $fatal(1, "CMO completed before Home");
        tick();
      end
      send_response(COMP, 2, 0, 0);
      expect_core_response(0, DATA_DESTINATION_NONE, 0);
      send_core_request(ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 1);
      // The existing dirty-snoop policy relinquishes its local copy, including
      // for CleanShared. Clean is allowed to invalidate after preserving data.
      accept_request(READ_CLEAN, ADDRESS, 0, 6, 1, 0);
      return_line(ADDRESS, operation == 7 ? LINE : evict_dirty_line, 3'b001);
      accept_comp_ack();
      expect_core_response(operation == 7 ? LINE[63:0] : STORE_DATA, DATA_DESTINATION_INTEGER, 1);
      assert (!tx_req_pending) else $fatal(1, "unexpected maintenance traffic");
      // A miss still travels to Home; a failed completion must be observable.
      send_core_request(ADDRESS + 64'h1000, 4'd9, ATOMIC_SWAP, 0, 0);
      for (int cycles = 0; !tx_req_pending && cycles < 100; cycles++) tick();
      assert (tx_req_pending && captured_req.opcode == 7'h09) else $fatal(1, "CMO miss was silently dropped");
      tx_req_pending = 0;
      send_response(COMP, 2, 0, 0, 2'b10);
      expect_core_response(0, DATA_DESTINATION_NONE, 0, 1);
    end
    // Start with two adjacent UniqueClean lines. Exercise every aligned W/D
    // reservation on both sides of a 64-byte boundary within one 128-byte block.
    reset = 1;
    repeat (2) tick();
    reset = 0;
    tx_req_pending = 0;
    tx_rsp_pending = 0;
    tx_dat_pending = 0;
    tick();
    assert (!core_out.reservation_valid) else $fatal(1, "reset retained reservation");
    for (int line_index = 0; line_index < 2; line_index++) begin
      send_core_request(ADDRESS + 64'(line_index * 64), MEMORY_LOAD, ATOMIC_SWAP, 0, 1);
      accept_request(READ_CLEAN, ADDRESS + 64'(line_index * 64), 0, 6, 1, 0);
      return_line(ADDRESS + 64'(line_index * 64), 0, 3'b010);
      accept_comp_ack();
      expect_core_response(0, DATA_DESTINATION_INTEGER, 1);
    end
    for (int size = 2; size <= 3; size++) begin
      for (int offset = 0; offset < 128; offset += (1 << size)) begin
        send_core_request(ADDRESS + 64'(offset), MEMORY_LR, ATOMIC_SWAP, 0, 1, 0, 2'b11, 2'(size));
        expect_core_response(0, DATA_DESTINATION_INTEGER, 1);
        assert (core_out.reservation_valid) else $fatal(1, "LR failed to establish reservation");
        // A neighboring line is outside the reservation, even in the same 128-byte block.
        send_core_request(ADDRESS + 64'(offset ^ 64), MEMORY_STORE, ATOMIC_SWAP, 0, 0, 0, 2'b11, 2'(size));
        expect_core_response(0, DATA_DESTINATION_NONE, 0);
        assert (core_out.reservation_valid) else $fatal(1, "neighboring line cleared reservation");
        send_core_request(ADDRESS + 64'(offset), MEMORY_SC, ATOMIC_SWAP, 64'h1234, 2, 0, 2'b11, 2'(size));
        expect_core_response(0, DATA_DESTINATION_INTEGER, 2);
        assert (!core_out.reservation_valid) else $fatal(1, "successful SC retained reservation");
        send_core_request(ADDRESS + 64'(offset), MEMORY_SC, ATOMIC_SWAP, 64'h5678, 2, 0, 2'b11, 2'(size));
        expect_core_response(1, DATA_DESTINATION_INTEGER, 2);
        send_core_request(ADDRESS + 64'(offset), MEMORY_LOAD, ATOMIC_SWAP, 0, 1, 0, 2'b11, 2'(size));
        expect_core_response(64'h1234, DATA_DESTINATION_INTEGER, 1);
        send_core_request(ADDRESS + 64'(offset), MEMORY_LR, ATOMIC_SWAP, 0, 1, 0, 2'b11, 2'(size));
        expect_core_response(64'h1234, DATA_DESTINATION_INTEGER, 1);
        send_core_request(ADDRESS + 64'(offset ^ (1 << size)), MEMORY_SC, ATOMIC_SWAP, 64'h5678, 2, 0, 2'b11, 2'(size));
        expect_core_response(1, DATA_DESTINATION_INTEGER, 2);
        assert (!core_out.reservation_valid && !tx_req_pending) else $fatal(1, "mismatched SC retained reservation or issued traffic");
        send_core_request(ADDRESS + 64'(offset ^ (1 << size)), MEMORY_LOAD, ATOMIC_SWAP, 0, 1, 0, 2'b11, 2'(size));
        expect_core_response(0, DATA_DESTINATION_INTEGER, 1);
        send_core_request(ADDRESS + 64'(offset), MEMORY_STORE, ATOMIC_SWAP, 0, 0, 0, 2'b11, 2'(size));
        expect_core_response(0, DATA_DESTINATION_NONE, 0);
      end
    end
    // Address equality alone is insufficient: a W reservation cannot authorize SC.D.
    send_core_request(ADDRESS, MEMORY_LR, ATOMIC_SWAP, 0, 1, 0, 2'b11, 2'd2);
    expect_core_response(0, DATA_DESTINATION_INTEGER, 1);
    send_core_request(ADDRESS, MEMORY_SC, ATOMIC_SWAP, 64'h5678, 2);
    expect_core_response(1, DATA_DESTINATION_INTEGER, 2);
    send_core_request(ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 1);
    expect_core_response(0, DATA_DESTINATION_INTEGER, 1);
    $display("RV64 reservation bounds passed: 48 aligned W/D sites, adjacent-line isolation, exact address/width, one-shot SC");
    // Exercise all nine AMOs through the cache, not only the standalone ALU.
    for (int size = 2; size <= 3; size++) begin
      for (int operation = 0; operation < 9; operation++) begin
        for (int sample = 0; sample < 4; sample++) begin
          logic [63:0] left_value, right_value, result, address, expected_word;
          left_value = amo_operand(sample);
          right_value = amo_operand(sample ^ 1);
          result = amo_reference(left_value, right_value, operation, size == 2);
          address = ADDRESS + (size == 2 ? ((sample & 1) != 0 ? 64'd4 : 64'd0) : 64'd56);
          expected_word = 64'hcafef00d_deadbeef;
          send_core_request(ADDRESS, MEMORY_STORE, ATOMIC_SWAP, expected_word, 0);
          expect_core_response(0, DATA_DESTINATION_NONE, 0);
          send_core_request(address, MEMORY_STORE, ATOMIC_SWAP, left_value, 0, 0, 2'b11, 2'(size));
          expect_core_response(0, DATA_DESTINATION_NONE, 0);
          send_core_request(address, MEMORY_ATOMIC, 4'(operation), right_value, 2, 0, 2'b11, 2'(size));
          expect_core_response(size == 2 ? {{32{left_value[31]}}, left_value[31:0]} : left_value, DATA_DESTINATION_INTEGER, 2);
          send_core_request(address, MEMORY_LOAD, ATOMIC_SWAP, 0, 1, 0, 2'b11, 2'(size));
          expect_core_response(result, DATA_DESTINATION_INTEGER, 1);
          if (size == 2) begin
            expected_word[(sample & 1) * 32 +: 32] = result[31:0];
            send_core_request(ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 1);
            expect_core_response(expected_word, DATA_DESTINATION_INTEGER, 1);
          end
        end
      end
    end
    // Request acceptance is not the AMO's linearization point. A contending
    // snoop may win first, but cannot expose a partial RMW or lose the request.
    send_core_request(ADDRESS, MEMORY_ZERO, ATOMIC_SWAP, 0, 0);
    expect_core_response(0, DATA_DESTINATION_NONE, 0);
    send_core_request(ADDRESS + 56, MEMORY_STORE, ATOMIC_SWAP, 1, 0);
    expect_core_response(0, DATA_DESTINATION_NONE, 0);
    watch_amo_response = 1;
    send_core_request(ADDRESS + 56, MEMORY_ATOMIC, ATOMIC_ADD, 2, 2);
    send_snoop(ADDRESS, 12'h07d);
    dirty_line = 0;
    for (beat = 0; beat < 4; beat++) begin
      if (beat == 3) begin
        for (int cycles = 0; !chi_out.request_data.valid && cycles < 100; cycles++) tick();
        assert (chi_out.request_data.valid && chi_out.request_data.bits.data[64 +: 64] inside {64'd1, 64'd3})
          else $fatal(1, "snoop exposed neither the old nor the complete new AMO value");
        dirty_line[448 +: 64] = chi_out.request_data.bits.data[64 +: 64];
      end
      accept_snoop_data(beat, dirty_line, 12'h07d);
    end
    if (dirty_line[448 +: 64] == 1) begin
      assert (amo_response_count == 0) else $fatal(1, "AMO completed while snoop observed the pre-RMW value");
      accept_request(READ_UNIQUE, ADDRESS, 0, 6, 1, 0);
      return_line(ADDRESS, dirty_line, 3'b010);
      accept_comp_ack();
    end
    for (int cycles = 0; amo_response_count == 0 && cycles < 100; cycles++) tick();
    watch_amo_response = 0;
    assert (amo_response_count == 1) else $fatal(1, "contended AMO completion count");
    if (dirty_line[448 +: 64] == 1) begin
      send_core_request(ADDRESS + 56, MEMORY_LOAD, ATOMIC_SWAP, 0, 1);
      expect_core_response(3, DATA_DESTINATION_INTEGER, 1);
    end
    for (int size = 2; size <= 3; size++) begin
      for (int operation = 0; operation < 9; operation++) begin
        logic [63:0] old_value, operand, address, result;
        logic [511:0] initial_line;
        reset = 1;
        repeat (2) tick();
        reset = 0;
        tx_req_pending = 0;
        tx_rsp_pending = 0;
        tx_dat_pending = 0;
        tick();
        old_value = amo_operand(0);
        operand = amo_operand(1);
        initial_line = {8{old_value}};
        address = ADDRESS + (size == 2 ? 64'd4 : 64'd56);
        if (size == 2) old_value = {{32{initial_line[63]}}, initial_line[63:32]};
        result = amo_reference(old_value, operand, operation, size == 2);
        send_core_request(address, MEMORY_ATOMIC, 4'(operation), operand, 2, 0, 2'b11, 2'(size));
        accept_request(READ_UNIQUE, ADDRESS, 0, 6, 1, 0);
        repeat (3) begin
          tick();
          assert (!core_out.response.valid) else $fatal(1, "AMO completed before ownership/data");
        end
        return_line(ADDRESS, initial_line, 3'b010);
        accept_comp_ack();
        expect_core_response(old_value, DATA_DESTINATION_INTEGER, 2);
        send_core_request(address, MEMORY_LOAD, ATOMIC_SWAP, 0, 1, 0, 2'b11, 2'(size));
        expect_core_response(result, DATA_DESTINATION_INTEGER, 1);
      end
    end
    $display("RV64 AMOArithmetic passed: 72 hit + 18 miss W/D cases, word-lane preservation, and contending snoop");
    // Model Home's invalidating probe for a read-only inclusive-LLC victim.
    // It may wait, but must observe SC's new dirty data, or run after a bounded
    // timeout. No write or successful SC by another participant is injected.
    for (int scenario = 0; scenario < 3; scenario++) begin
      logic [511:0] progress_line;
      reset = 1;
      chi_in.snoops = '0;
      chi_in.request_data.ready = 0;
      prefetch_in = '0;
      repeat (2) tick();
      reset = 0;
      tx_req_pending = 0;
      tx_rsp_pending = 0;
      tx_dat_pending = 0;
      tick();
      progress_line = LINE;
      // Cover both an absent LR line and the shared-hit ownership upgrade.
      if (scenario == 1) begin
        send_core_request(ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 1);
        accept_request(READ_CLEAN, ADDRESS, 0, 6, 1, 0);
        return_line(ADDRESS, LINE, 3'b001);
        accept_comp_ack();
        expect_core_response(LINE[63:0], DATA_DESTINATION_INTEGER, 1);
      end
      send_core_request(ADDRESS, MEMORY_LR, ATOMIC_SWAP, 0, 1);
      accept_request(READ_UNIQUE, ADDRESS, 0, 6, 1, 0);
      repeat (12) begin
        assert (!core_out.response.valid && !core_out.reservation_valid)
          else $fatal(1, "LR completed before exclusive acquisition");
        tick();
      end
      return_line(ADDRESS, LINE, 3'b010);
      // The earliest post-grant probe must not slip between CompAck and install.
      progress_snoop_accepts = 0;
      watch_progress_snoop = 1;
      forbid_progress_snoop = 1;
      chi_in.snoops.bits = '0;
      chi_in.snoops.bits.address = ADDRESS[43:3];
      chi_in.snoops.bits.opcode = SNP_CLEAN_INVALID;
      chi_in.snoops.bits.txn_id = 12'h079;
      chi_in.snoops.bits.src_id = HOME_ID;
      chi_in.snoops.valid = 1;
      accept_comp_ack();
      expect_core_response(LINE[63:0], DATA_DESTINATION_INTEGER, 1);
      assert (core_out.reservation_valid) else $fatal(1, "owned LR did not reserve");
      if (scenario == 0) begin
        // Leave ample time for sixteen scalar instructions while probes and
        // best-effort colliding prefetches remain continuously offered.
        prefetch_in = '{valid: 1, bits: '{address: THIRD_ADDRESS, operation: 2'd2}};
        repeat (80) tick();
        prefetch_in = '0;
        assert (!tx_req_pending) else $fatal(1, "prefetch disturbed a protected LR");
        forbid_progress_snoop = 0;
        send_core_request(ADDRESS, MEMORY_SC, ATOMIC_SWAP, STORE_DATA, 2);
        expect_core_response(0, DATA_DESTINATION_INTEGER, 2);
        progress_line[63:0] = STORE_DATA;
      end else begin
        forbid_progress_snoop = 0;
        if (scenario == 2) begin
          // Repeated LR is not allowed to renew a probe-blocking reservation.
          send_core_request(ADDRESS, MEMORY_LR, ATOMIC_SWAP, 0, 1);
          expect_core_response(LINE[63:0], DATA_DESTINATION_INTEGER, 1);
        end
      end
      for (int cycle = 0; progress_snoop_accepts == 0 && cycle < 160; cycle++) tick();
      assert (progress_snoop_accepts == 1 && !tx_req_pending)
        else $fatal(1, "reservation starved a probe or SC attempted reacquisition");
      watch_progress_snoop = 0;
      if (scenario == 0) begin
        for (int packet = 0; packet < 4; packet++) accept_snoop_data(packet, progress_line, 12'h079);
      end else begin
        for (int cycle = 0; !tx_rsp_pending && cycle < 100; cycle++) tick();
        assert (tx_rsp_pending && captured_rsp.opcode == 1 && captured_rsp.resp == 0 && captured_rsp.txn_id == 12'h079)
          else $fatal(1, "expired protection failed clean probe completion");
        tx_rsp_pending = 0;
      end
      assert (!core_out.reservation_valid) else $fatal(1, "completed invalidating probe retained reservation");
      send_core_request(ADDRESS, MEMORY_SC, ATOMIC_SWAP, STORE_DATA_2, 2);
      expect_core_response(1, DATA_DESTINATION_INTEGER, 2);
      assert (!tx_req_pending) else $fatal(1, "revoked SC issued a refill");
      // An intervening writer can now supply a new value. Failed SC must not
      // carry its old authorization across that subsequent acquisition.
      send_core_request(ADDRESS, MEMORY_LR, ATOMIC_SWAP, 0, 1);
      accept_request(READ_UNIQUE, ADDRESS, 0, 6, 1, 0);
      progress_line[63:0] = STORE_DATA_2;
      return_line(ADDRESS, progress_line, 3'b010);
      accept_comp_ack();
      expect_core_response(STORE_DATA_2, DATA_DESTINATION_INTEGER, 1);
      send_core_request(ADDRESS, MEMORY_SC, ATOMIC_SWAP, STORE_DATA, 2);
      expect_core_response(0, DATA_DESTINATION_INTEGER, 2);
    end
    $display("LR/SC progress passed: exclusive LR, shared upgrade, post-grant probe, delayed local SC, timeout, repeated LR, reacquisition");
    // Timer expiry permits snoops; it does not revoke ownership by itself.
    // Cover W/D delays longer than the ACT sequence that crossed the window.
    for (int size = 2; size <= 3; size++) begin
      send_core_request(ADDRESS, MEMORY_LR, ATOMIC_SWAP, 0, 1, 0, 2'b11, 2'(size));
      expect_core_response(size == 2 ? {{32{STORE_DATA[31]}}, STORE_DATA[31:0]} : STORE_DATA, DATA_DESTINATION_INTEGER, 1);
      repeat (192) tick();
      assert (core_out.reservation_valid) else $fatal(1, "quiet delayed LR lost its reservation");
      // A repeated LR records a reservation even during an existing window.
      send_core_request(ADDRESS, MEMORY_LR, ATOMIC_SWAP, 0, 1, 0, 2'b11, 2'(size));
      expect_core_response(size == 2 ? {{32{STORE_DATA[31]}}, STORE_DATA[31:0]} : STORE_DATA, DATA_DESTINATION_INTEGER, 1);
      send_core_request(ADDRESS, MEMORY_LR, ATOMIC_SWAP, 0, 1, 0, 2'b11, 2'(size));
      expect_core_response(size == 2 ? {{32{STORE_DATA[31]}}, STORE_DATA[31:0]} : STORE_DATA, DATA_DESTINATION_INTEGER, 1);
      repeat (192) tick();
      send_core_request(ADDRESS, MEMORY_SC, ATOMIC_SWAP, STORE_DATA, 2, 0, 2'b11, 2'(size));
      expect_core_response(0, DATA_DESTINATION_INTEGER, 2);
      assert (!tx_req_pending) else $fatal(1, "quiet delayed SC acquired ownership");
    end
    // Start with no reservation or post-grant protection. Continuous probes
    // of an unrelated line must still give a waiting local LR a lookup turn.
    reset = 1;
    repeat (2) tick();
    reset = 0;
    tx_req_pending = 0;
    tx_rsp_pending = 0;
    tx_dat_pending = 0;
    tick();
    send_core_request(ADDRESS, MEMORY_LOAD, ATOMIC_SWAP, 0, 1);
    accept_request(READ_CLEAN, ADDRESS, 0, 6, 1, 0);
    return_line(ADDRESS, LINE, 3'b010);
    accept_comp_ack();
    expect_core_response(LINE[63:0], DATA_DESTINATION_INTEGER, 1);
    repeat (12) tick();
    chi_in.snoops.bits = '0;
    chi_in.snoops.bits.address = THIRD_ADDRESS[43:3];
    chi_in.snoops.bits.opcode = SNP_CLEAN_INVALID;
    chi_in.snoops.bits.txn_id = 12'h07a;
    chi_in.snoops.bits.src_id = HOME_ID;
    chi_in.snoops.valid = 1;
    repeat (12) tick();
    send_core_request(ADDRESS, MEMORY_LR, ATOMIC_SWAP, 0, 1);
    expect_core_response(LINE[63:0], DATA_DESTINATION_INTEGER, 1);
    assert (core_out.reservation_valid && !chi_out.snoops.ready && !tx_req_pending)
      else $fatal(1, "continuous unrelated probes starved local LR");
    send_core_request(ADDRESS, MEMORY_SC, ATOMIC_SWAP, STORE_DATA, 2);
    expect_core_response(0, DATA_DESTINATION_INTEGER, 2);
    chi_in.snoops = '0;
    $display("LR admission under continuous unrelated probes passed");

    reset=1; chi_in.snoops='0; repeat(2) tick(); reset=0;
    tx_req_pending=0; tx_rsp_pending=0; tx_dat_pending=0;
    tick();
    send_core_request(ADDRESS,MEMORY_LOAD,ATOMIC_SWAP,0,1);
    accept_request(READ_CLEAN,ADDRESS,0,6,1,0);
    return_line(ADDRESS,LINE,3'b010); accept_comp_ack();
    expect_core_response(LINE[63:0],DATA_DESTINATION_INTEGER,1);
    // A squashed store candidate has no architectural or array effect.
    stage_pipeline_store(ADDRESS,STORE_DATA);
    tick(); pipeline_in='0; repeat(3) tick();
    check_pipeline_load(ADDRESS,1,1,LINE[63:0]);
    // Same word / disjoint bytes, different word, and actual byte overlap.
    store_then_load(ADDRESS,64'hbeef,ADDRESS+2,2'd1,2'd1,0,64'h4433);
    check_pipeline_load(ADDRESS,1,1,64'h887766554433beef);
    store_then_load(ADDRESS,64'h1234,ADDRESS+8,2'd1,2'd3,0,LINE[127:64]);
    store_then_load(ADDRESS,64'h5678,ADDRESS,2'd1,2'd1,1,0);
    check_pipeline_load(ADDRESS,1,1,64'h8877665544335678);
    // Same page-offset, different physical tag is not a dependency.
    send_core_request(EVICT_ADDRESS,MEMORY_LOAD,ATOMIC_SWAP,0,1);
    accept_request(READ_CLEAN,EVICT_ADDRESS,0,6,1,0);
    return_line(EVICT_ADDRESS,EVICT_LINE,3'b010); accept_comp_ack();
    expect_core_response(EVICT_LINE[63:0],DATA_DESTINATION_INTEGER,1);
    store_then_load(ADDRESS,64'habcd,EVICT_ADDRESS,2'd1,2'd3,0,EVICT_LINE[63:0]);
    stage_pipeline_store(ADDRESS,STORE_DATA);
    tick(); pipeline_in.request.valid=0; pipeline_in.commit=1;
    chi_in.snoops.bits='0;
    chi_in.snoops.bits.address=THIRD_ADDRESS[43:3];
    chi_in.snoops.bits.opcode=SNP_CLEAN_INVALID;
    chi_in.snoops.bits.txn_id=12'h077;
    chi_in.snoops.bits.src_id=HOME_ID;
    chi_in.snoops.valid=1;
    #1;
    assert(pipeline_out.commit_ready) else $fatal(1,"unrelated snoop rejected a store");
    tick(); pipeline_in='0; chi_in.snoops='0;
    repeat(8) tick(); tx_rsp_pending=0;
    check_pipeline_load(ADDRESS,1,1,STORE_DATA);
    // A matching snoop must see the committed bytes, even when it arrives
    // immediately after enqueue and before an ordinary idle drain slot.
    stage_pipeline_store(ADDRESS,STORE_DATA);
    tick(); pipeline_in.request.valid=0; pipeline_in.commit=1;
    #1; assert(pipeline_out.commit_ready) else $fatal(1,"store commit before snoop");
    tick(); pipeline_in='0;
    send_snoop(ADDRESS,12'h079);
    dirty_line=LINE; dirty_line[63:0]=STORE_DATA;
    for(beat=0;beat<4;beat++) accept_snoop_data(beat,dirty_line,12'h079);
    check_pipeline_load(ADDRESS,1,0);
    assert(core_out.drained) else $fatal(1,"snooped store did not drain");
    // A probe between MEM proof and WB authorization rejects the candidate.
    stage_pipeline_store(EVICT_ADDRESS,STORE_DATA);
    tick(); pipeline_in.request.valid=0; pipeline_in.commit=1;
    chi_in.snoops.bits='0;
    chi_in.snoops.bits.address=EVICT_ADDRESS[43:3];
    chi_in.snoops.bits.opcode=SNP_CLEAN_INVALID;
    chi_in.snoops.bits.txn_id=12'h078;
    chi_in.snoops.bits.src_id=HOME_ID;
    chi_in.snoops.valid=1;
    #1;
    assert(!pipeline_out.commit_ready) else $fatal(1,"stale store proof survived a probe");
    tick(); pipeline_in='0; chi_in.snoops='0;
    repeat(8) tick();
    assert(tx_rsp_pending && captured_rsp.opcode==1 && captured_rsp.txn_id==12'h078 && !tx_dat_pending)
      else $fatal(1,"rejected store dirtied the probed line");
    tx_rsp_pending=0;
    $display("WB stores: squash, byte hazards, physical tags, independent hits, and coherent draining passed");

    // One ordinary miss permits sustained read hits in other sets, including
    // protocol retry and gapped packets. Neither another miss nor a store can
    // claim the occupied transaction slot or mutate a reserved way.
    for(int store_miss=0;store_miss<2;store_miss++) begin
      int replies, blocked, resumed;
      prepare_hit_under_miss();
      send_core_request(THIRD_ADDRESS,store_miss!=0 ? MEMORY_STORE : MEMORY_LOAD,ATOMIC_SWAP,STORE_DATA,2);
      accept_request(store_miss!=0 ? READ_UNIQUE : READ_CLEAN,THIRD_ADDRESS,0,6,1,0);
      stream_under_miss();
      check_under_miss(ADDRESS,PIPE_REPLAY); // Victim still has its old tag.
      check_under_miss(EVICT_ADDRESS,PIPE_REPLAY); // Other way in the reserved set.
      check_under_miss(THIRD_ADDRESS,PIPE_REPLAY); // Incoming line.
      check_under_miss(PREFETCH_WRITE_ADDRESS,PIPE_REPLAY); // Second miss, another set.
      check_under_miss(PREFETCH_READ_ADDRESS,PIPE_REPLAY,0,MEMORY_STORE);
      send_response(RETRY_ACK,0,0,3);
      stream_under_miss();
      send_response(PCRD_GRANT,0,0,3);
      accept_request(store_miss!=0 ? READ_UNIQUE : READ_CLEAN,THIRD_ADDRESS,0,6,0,3);
      chi_in.requester_responses.ready=0;
      return_line(THIRD_ADDRESS,THIRD_LINE,store_miss!=0 ? 3'b010 : 3'b001,1);
      stream_under_miss(); // Full line buffered, CompAck backpressured.
      pipeline_lookup_in='{valid:1,bits:'{address:PREFETCH_READ_ADDRESS ^ virtual_page_xor,access:MEMORY_LOAD,width:3,unsigned_0:0,data:0}};
      tick();
      pipeline_in.request='{valid:1,bits:'{address:PREFETCH_READ_ADDRESS,access:MEMORY_LOAD,width:3,unsigned_0:0,data:0}};
      grant_rsp_credit(); tick(); accept_comp_ack();
      replies=0; blocked=0; resumed=0;
      repeat(24) begin
        #1;
        if(pipeline_out.response.bits.outcome==PIPE_LOAD_HIT) begin
          assert(pipeline_out.response.bits.data==LINE[63:0]) else $fatal(1,"installation corrupted an independent hit");
          resumed++;
        end else begin
          assert(pipeline_out.response.bits.outcome==PIPE_REPLAY) else $fatal(1,"installation lookup allocated work");
          blocked++;
        end
        if(core_out.response.valid) begin
          assert(core_out.response.bits.rd==2 && core_out.response.bits.data==(store_miss!=0 ? 0 : THIRD_LINE[63:0]))
            else $fatal(1,"miss result mixed with speculative hit");
          replies++;
        end
        tick();
      end
      pipeline_lookup_in='0; pipeline_in='0; tick();
      assert(replies==1 && blocked>=8 && resumed>=8 && core_out.drained && !tx_req_pending)
        else $fatal(1,"refill progress under streaming hits: replies=%0d blocked=%0d resumed=%0d",replies,blocked,resumed);
      check_pipeline_load(THIRD_ADDRESS,1,1,store_miss!=0 ? STORE_DATA : THIRD_LINE[63:0]);
      check_pipeline_load(EVICT_ADDRESS,1,1,EVICT_LINE[63:0]);
    end

    // Non-allocating refill completion can coincide with an independent hit.
    begin
      int simultaneous;
      prepare_hit_under_miss();
      send_core_request(THIRD_ADDRESS,MEMORY_LOAD,ATOMIC_SWAP,0,2,1);
      accept_request(READ_CLEAN,THIRD_ADDRESS,0,6,1,0);
      chi_in.requester_responses.ready=0;
      return_line(THIRD_ADDRESS,THIRD_LINE,3'b001,1);
      pipeline_lookup_in='{valid:1,bits:'{address:PREFETCH_READ_ADDRESS ^ virtual_page_xor,access:MEMORY_LOAD,width:3,unsigned_0:0,data:0}};
      tick();
      pipeline_in.request='{valid:1,bits:'{address:PREFETCH_READ_ADDRESS,access:MEMORY_LOAD,width:3,unsigned_0:0,data:0}};
      grant_rsp_credit(); tick(); accept_comp_ack(); simultaneous=0;
      repeat(8) begin
        #1;
        assert(pipeline_out.response.bits.outcome==PIPE_LOAD_HIT && pipeline_out.response.bits.data==LINE[63:0])
          else $fatal(1,"non-allocating completion stalled/corrupted a hit");
        if(core_out.response.valid) begin
          assert(core_out.response.bits.data==THIRD_LINE[63:0] && core_out.response.bits.rd==2)
            else $fatal(1,"non-allocating completion lost its destination/data");
          simultaneous++;
        end
        tick();
      end
      pipeline_lookup_in='0; pipeline_in='0; tick();
      assert(simultaneous==1 && core_out.drained) else $fatal(1,"missing concurrent completion");
      check_pipeline_load(THIRD_ADDRESS,1,0);
    end

    // An older queued mutation remains an ordering barrier to speculative hits.
    prepare_hit_under_miss();
    send_core_request(THIRD_ADDRESS,MEMORY_LOAD,ATOMIC_SWAP,0,2);
    accept_request(READ_CLEAN,THIRD_ADDRESS,0,6,1,0);
    send_core_request(PREFETCH_READ_ADDRESS,MEMORY_STORE,ATOMIC_SWAP,STORE_DATA,0);
    check_under_miss(PREFETCH_READ_ADDRESS,PIPE_REPLAY);
    return_line(THIRD_ADDRESS,THIRD_LINE,3'b001); accept_comp_ack();
    expect_core_response(THIRD_LINE[63:0],DATA_DESTINATION_INTEGER,2);
    expect_core_response(0,DATA_DESTINATION_NONE,0);
    check_pipeline_load(PREFETCH_READ_ADDRESS,1,1,STORE_DATA);

    // Dirty victim transmission no longer monopolizes idle SRAM cycles.
    prepare_hit_under_miss();
    send_core_request(ADDRESS,MEMORY_STORE,ATOMIC_SWAP,STORE_DATA,0);
    expect_core_response(0,DATA_DESTINATION_NONE,0);
    dirty_line=LINE; dirty_line[63:0]=STORE_DATA;
    send_core_request(THIRD_ADDRESS,MEMORY_LOAD,ATOMIC_SWAP,0,2);
    accept_request(WRITE_BACK_FULL,ADDRESS,1,6,1,0);
    check_under_miss(PREFETCH_READ_ADDRESS,PIPE_LOAD_HIT,LINE[63:0]);
    check_under_miss(ADDRESS,PIPE_REPLAY);
    send_response(COMP_DBID_RESP,1,12'h055,0);
    for(beat=0;beat<4;beat++) accept_copyback_data(beat,dirty_line);
    accept_request(READ_CLEAN,THIRD_ADDRESS,0,6,1,0);
    stream_under_miss();
    // An invalidating probe wins over the speculative stream, without waiting
    // for the unrelated refill. Once invalidated, that load must replay.
    send_snoop(PREFETCH_READ_ADDRESS,12'h07f);
    for(int cycle=0;!tx_rsp_pending && cycle<32;cycle++) begin
      pipeline_lookup_in='{valid:1,bits:'{address:PREFETCH_READ_ADDRESS ^ virtual_page_xor,access:MEMORY_LOAD,width:3,unsigned_0:0,data:0}};
      tick();
    end
    pipeline_lookup_in='0;
    assert(tx_rsp_pending && captured_rsp.txn_id==12'h07f && captured_rsp.resp==0)
      else $fatal(1,"hit-under-miss starved an invalidating probe");
    tx_rsp_pending=0;
    check_under_miss(PREFETCH_READ_ADDRESS,PIPE_REPLAY);
    return_line(THIRD_ADDRESS,THIRD_LINE,3'b001); accept_comp_ack();
    expect_core_response(THIRD_LINE[63:0],DATA_DESTINATION_INTEGER,2);

    // LR/atomic acquisition is still globally serialized; reset retires the
    // reservation bookkeeping even with an unanswered ordinary miss.
    for(int atomic_miss=0;atomic_miss<2;atomic_miss++) begin
      prepare_hit_under_miss();
      send_core_request(THIRD_ADDRESS,atomic_miss!=0 ? MEMORY_ATOMIC : MEMORY_LR,ATOMIC_SWAP,STORE_DATA,2);
      accept_request(READ_UNIQUE,THIRD_ADDRESS,0,6,1,0);
      check_under_miss(PREFETCH_READ_ADDRESS,PIPE_REPLAY);
      return_line(THIRD_ADDRESS,THIRD_LINE,3'b010); accept_comp_ack();
      expect_core_response(THIRD_LINE[63:0],DATA_DESTINATION_INTEGER,2);
    end
    prepare_hit_under_miss();
    send_core_request(THIRD_ADDRESS,MEMORY_LOAD,ATOMIC_SWAP,0,2);
    accept_request(READ_CLEAN,THIRD_ADDRESS,0,6,1,0);
    prepare_hit_under_miss();
    check_pipeline_load(PREFETCH_READ_ADDRESS,1,1,LINE[63:0]);
    $display("Load hit-under-miss: streaming hits, reserved sets, retry, installation, concurrent completion, queued stores, writeback, snoops, and reset passed");

    // A simultaneous demand wins over a one-cycle hint. The hint is dropped,
    // not saved for later admission when the demand has completed.
    prefetch_in = '{valid:1, bits:'{address:PREFETCH_WRITE_ADDRESS, operation:2'd2}};
    send_core_request(PREFETCH_READ_ADDRESS,MEMORY_LOAD,ATOMIC_SWAP,0,6);
    prefetch_in = '0;
    expect_core_response(LINE[63:0],DATA_DESTINATION_INTEGER,6);
    repeat(12) begin
      tick();
      assert(!tx_req_pending && !core_out.response.valid && core_out.drained)
        else $fatal(1,"simultaneous prefetch displaced a demand or survived rejection");
    end

    // Hints offered while miss service blocks S3 must disappear. A younger
    // authorized demand remains retained and completes after the miss instead.
    send_core_request(THIRD_ADDRESS,MEMORY_LOAD,ATOMIC_SWAP,0,2);
    accept_request(READ_CLEAN,THIRD_ADDRESS,0,6,1,0);
    send_prefetch(PREFETCH_WRITE_ADDRESS,2'd3);
    prefetch_in = '{valid:1, bits:'{address:PREFETCH_WRITE_ADDRESS, operation:2'd2}};
    send_core_request(PREFETCH_READ_ADDRESS+8,MEMORY_LOAD,ATOMIC_SWAP,0,7);
    prefetch_in = '0;
    repeat(4) begin
      tick();
      assert(!tx_req_pending && !core_out.response.valid)
        else $fatal(1,"blocked prefetch bypassed miss ownership");
    end
    return_line(THIRD_ADDRESS,THIRD_LINE,3'b001); accept_comp_ack();
    expect_core_response(THIRD_LINE[63:0],DATA_DESTINATION_INTEGER,2);
    expect_core_response(LINE[127:64],DATA_DESTINATION_INTEGER,7);
    repeat(12) begin
      tick();
      assert(!tx_req_pending && !core_out.response.valid && core_out.drained)
        else $fatal(1,"blocked prefetch was retained or queued demand was duplicated");
    end
    $display("Flow admission: demand priority, blocked hint drops, and retained demand completion passed");
    // A competing transaction snoops the victim while WriteBackFull awaits
    // its grant. Return the authoritative dirty bytes once, then an Invalid
    // copyback; the saved victim buffer must never resurrect the old version.
    prepare_hit_under_miss();
    send_core_request(ADDRESS,MEMORY_STORE,ATOMIC_SWAP,STORE_DATA,0);
    expect_core_response(0,DATA_DESTINATION_NONE,0);
    dirty_line=LINE; dirty_line[63:0]=STORE_DATA;
    send_core_request(THIRD_ADDRESS,MEMORY_LOAD,ATOMIC_SWAP,0,2);
    accept_request(WRITE_BACK_FULL,ADDRESS,1,6,1,0);
    chi_in.request_data.ready=0;
    send_snoop(ADDRESS,12'h078);
    for(beat=0;beat<4;beat++) accept_snoop_data(beat,dirty_line,12'h078);
    // Force a different snoop lookup after the victim gather; eviction identity
    // must come from the retained address/context, not mutable gather registers.
    send_snoop(PREFETCH_READ_ADDRESS,12'h079);
    for(int cycle=0;!tx_rsp_pending && cycle<100;cycle++) tick();
    assert(tx_rsp_pending) else $fatal(1,"unrelated snoop failed during copyback");
    tx_rsp_pending=0;
    grant_dat_credit();
    send_response(COMP_DBID_RESP,1,12'h055,0);
    for(beat=0;beat<4;beat++) accept_copyback_data(beat,dirty_line,0);
    accept_request(READ_CLEAN,THIRD_ADDRESS,0,6,1,0);
    return_line(THIRD_ADDRESS,THIRD_LINE,3'b001); accept_comp_ack();
    expect_core_response(THIRD_LINE[63:0],DATA_DESTINATION_INTEGER,2);
    check_pipeline_load(ADDRESS,1,0);
    $display("RV5Stage VIPT write-back cache, copyback snoop races, and maintenance passed");
    $finish;
  end
endmodule
