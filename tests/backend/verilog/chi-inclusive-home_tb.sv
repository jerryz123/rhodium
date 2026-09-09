// Checks inclusive fills, resident-only eviction, and complete cached/victim packets.
module chi_inclusive_home_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_t;
  typedef struct packed { logic valid; CHISnoopDispatch bits; } snoop_t;
  typedef struct packed {
    req_t requests;
    rsp_t requester_responses;
    dat_t request_data;
    ready_t responses;
    ready_t response_data;
    ready_t snoops;
  } requester_in_t;
  typedef struct packed {
    ready_t requests;
    ready_t requester_responses;
    ready_t request_data;
    rsp_t responses;
    dat_t response_data;
    snoop_t snoops;
  } requester_out_t;
  typedef struct packed {
    rsp_t rsp;
    ready_t req;
    struct packed { ready_t request; dat_t response; } dat;
  } subordinate_in_t;
  typedef struct packed {
    ready_t rsp;
    req_t req;
    struct packed { dat_t request; ready_t response; } dat;
  } subordinate_out_t;
  typedef struct packed { requester_in_t requester; subordinate_in_t subordinate; } hnf_in_t;
  typedef struct packed { requester_out_t requester; subordinate_out_t subordinate; } hnf_out_t;

  localparam logic [6:0] READ_NO_SNP = 7'h04;
  localparam logic [6:0] WRITE_NO_SNP_FULL = 7'h1d;
  localparam logic [6:0] WRITE_NO_SNP_PTL = 7'h1c;
  localparam logic [6:0] WRITE_UNIQUE_PTL = 7'h18;
  localparam logic [4:0] SNP_RESP = 5'h01;
  localparam logic [4:0] COMP = 5'h04;
  localparam logic [4:0] DBID_RESP = 5'h06;
  localparam logic [4:0] SNP_CLEAN_INVALID = 5'h09;
  localparam logic [3:0] SNP_RESP_DATA_PTL = 4'h5;
  localparam logic [3:0] NON_COPY_BACK_WRITE_DATA = 4'h3;
  localparam logic [3:0] COMP_DATA = 4'h4;
  localparam logic [6:0] HTIF_ID = 7'h01;
  localparam logic [6:0] INSTRUCTION_ID = 7'h02;
  localparam logic [6:0] DATA_ID = 7'h03;
  localparam logic [6:0] HOME_ID = 7'h05;
  localparam logic [6:0] MEMORY_ID = 7'h09;
  localparam logic [11:0] MEMORY_DBID = 12'h055;
  localparam logic [43:0] LINE0 = 44'h080000000;
  localparam logic [43:0] LINE1 = 44'h080000100;
  localparam logic [43:0] LINE2 = 44'h080000200;
  localparam logic [43:0] LINE3 = 44'h080000400;
  localparam logic [127:0] PARTIAL_DATA = 128'hf0e1d2c3b4a5968778695a4b3c2d1e0f;
  localparam logic [15:0] PARTIAL_MASK = 16'ha55a;
  localparam logic [63:0] SNOOP_MASKS = 64'hffff_8001_5aa5_0001;

  logic clock = 1'b0;
  logic reset = 1'b1;
  CHIHNFIdentity identity;
  req_t requester_requests_in;
  rsp_t requester_responses_in;
  dat_t request_data_in;
  ready_t requester_responses_ready_in;
  ready_t response_data_ready_in;
  ready_t snoops_ready_in;
  rsp_t subordinate_responses_in;
  ready_t subordinate_requests_ready_in;
  ready_t subordinate_data_ready_in;
  dat_t subordinate_data_in;
  hnf_in_t port_in;
  hnf_out_t port_out;
  logic [127:0] expected_line [4];
  CHIReqFlit active_request;
  always @(posedge clock)
    if (!reset && requester_requests_in.valid && port_out.requester.requests.ready)
      active_request <= requester_requests_in.bits;

  assign port_in.requester.requests = requester_requests_in;
  assign port_in.requester.requester_responses = requester_responses_in;
  assign port_in.requester.request_data = request_data_in;
  assign port_in.requester.responses = requester_responses_ready_in;
  assign port_in.requester.response_data = response_data_ready_in;
  assign port_in.requester.snoops = snoops_ready_in;
  assign port_in.subordinate.rsp = subordinate_responses_in;
  assign port_in.subordinate.req = subordinate_requests_ready_in;
  assign port_in.subordinate.dat.request = subordinate_data_ready_in;
  assign port_in.subordinate.dat.response = subordinate_data_in;

  CHIInclusiveHNF dut (.*);
  always #5 clock = ~clock;
  initial begin
    #100000;
    $fatal(1, "inclusive Home simulation timed out");
  end

  task automatic tick;
    begin
      @(posedge clock);
      #1;
    end
  endtask

  task automatic send_request(input logic [43:0] address,
                              input logic [6:0] opcode,
                              input logic [5:0] request_size = 6'd6,
                              input logic [6:0] source = HTIF_ID,
                              input bit exp_comp_ack = 0);
    begin
      requester_requests_in.bits = '0;
      requester_requests_in.bits.src_id = source;
      requester_requests_in.bits.tgt_id = HOME_ID;
      requester_requests_in.bits.opcode = opcode;
      requester_requests_in.bits.address = address;
      requester_requests_in.bits.size_or_num_req = request_size;
      requester_requests_in.bits.return_nid_or_stash_nid_or_data_target = source;
      requester_requests_in.bits.return_txn_id_or_stash_lpid = 12'h654;
      requester_requests_in.bits.trace_tag = 1;
      requester_requests_in.bits.exp_comp_ack = exp_comp_ack;
      requester_requests_in.bits.qos = 4'ha;
      requester_requests_in.valid = 1'b1;
      #1;
      assert (port_out.requester.requests.ready)
        else $fatal(1, "inclusive Home did not accept an idle request");
      tick();
      requester_requests_in = '0;
    end
  endtask

  task automatic accept_memory_request(input logic [43:0] address,
                                       input logic [6:0] opcode);
    begin
      subordinate_requests_ready_in.ready = 1'b1;
      #1;
      assert (port_out.subordinate.req.valid &&
              port_out.subordinate.req.bits.address == address &&
              port_out.subordinate.req.bits.size_or_num_req == 6'd6 &&
              port_out.subordinate.req.bits.opcode == opcode)
        else $fatal(1, "inclusive Home issued an incorrect memory request");
      tick();
      subordinate_requests_ready_in = '0;
    end
  endtask

  task automatic return_fill_packet(input logic [1:0] packet_id,
                                    input logic [7:0] payload,
                                    input logic [1:0] error = 0);
    begin
      subordinate_data_in.bits = '0;
      subordinate_data_in.bits.opcode = COMP_DATA;
      subordinate_data_in.bits.resp_err = error;
      subordinate_data_in.bits.src_id = MEMORY_ID;
      subordinate_data_in.bits.tgt_id = HOME_ID;
      subordinate_data_in.bits.data_id = packet_id;
      subordinate_data_in.bits.byte_enable = 16'hffff;
      subordinate_data_in.bits.data = {120'h0, payload};
      subordinate_data_in.valid = 1'b1;
      #1;
      assert (port_out.subordinate.dat.response.ready)
        else $fatal(1, "inclusive Home did not accept fill data");
      tick();
      subordinate_data_in = '0;
    end
  endtask

  task automatic accept_cached_packet(input logic [1:0] packet_id,
                                      input logic [127:0] payload,
                                      input logic [1:0] error = 0);
    CHIDatFlit expected_packet;
    begin
      expected_packet = '0;
      expected_packet.data = payload;
      expected_packet.resp_err = error;
      for (int b = 0; b < 16; b++)
        expected_packet.byte_enable[b] = active_request.size_or_num_req >= 4 ||
          (b >= int'(active_request.address[3:0]) && b < int'(active_request.address[3:0]) + (1 << active_request.size_or_num_req));
      expected_packet.data_id = packet_id;
      expected_packet.trace_tag = active_request.trace_tag;
      expected_packet.qos = active_request.qos;
      expected_packet.opcode = COMP_DATA;
      expected_packet.home_nid_or_pbha_or_mismatched_mecid = HOME_ID;
      expected_packet.src_id = HOME_ID;
      expected_packet.tgt_id = active_request.return_nid_or_stash_nid_or_data_target;
      expected_packet.txn_id = active_request.return_txn_id_or_stash_lpid;
      if (error == 0 && (active_request.opcode == 7'h02 || active_request.opcode == 7'h07)) expected_packet.resp = active_request.opcode == 7'h07 ? 3'd2 : 3'd1;
      response_data_ready_in.ready = 1'b1;
      #1;
      assert(port_out.requester.response_data.bits === expected_packet) else $fatal(1, "complete cached DAT mismatch");
      assert (port_out.requester.response_data.valid &&
              port_out.requester.response_data.bits.data_id == packet_id &&
              port_out.requester.response_data.bits.data == payload &&
              port_out.requester.response_data.bits.resp_err == error)
        else $fatal(1, "inclusive Home returned incorrect cached data");
      tick();
      response_data_ready_in = '0;
    end
  endtask

  task automatic fill_and_return(input logic [43:0] address,
                                 input logic [7:0] payload_base);
    begin
      accept_memory_request(address, READ_NO_SNP);
      return_fill_packet(2'd0, payload_base + 0);
      return_fill_packet(2'd1, payload_base + 1);
      return_fill_packet(2'd2, payload_base + 2);
      return_fill_packet(2'd3, payload_base + 3);
      accept_cached_packet(2'd0, 128'(payload_base) + 128'd0);
      accept_cached_packet(2'd1, 128'(payload_base) + 128'd1);
      accept_cached_packet(2'd2, 128'(payload_base) + 128'd2);
      accept_cached_packet(2'd3, 128'(payload_base) + 128'd3);
    end
  endtask

  task automatic send_write_packet(input logic [1:0] packet_id,
                                   input logic [127:0] payload,
                                   input logic [15:0] byte_mask = 16'hffff);
    begin
      request_data_in.bits = '0;
      request_data_in.bits.opcode = NON_COPY_BACK_WRITE_DATA;
      request_data_in.bits.src_id = HTIF_ID;
      request_data_in.bits.tgt_id = HOME_ID;
      request_data_in.bits.data_id = packet_id;
      request_data_in.bits.byte_enable = byte_mask;
      request_data_in.bits.data = payload;
      request_data_in.valid = 1'b1;
      #1;
      assert (port_out.requester.request_data.ready)
        else $fatal(1, "inclusive Home did not accept requester write data");
      tick();
      request_data_in = '0;
    end
  endtask

  task automatic clean_snoop(input logic [6:0] target,
                            input logic [4:0] opcode = SNP_CLEAN_INVALID,
                            input logic [2:0] state = 0,
                            input logic [1:0] error = 0);
    begin
      while (!port_out.requester.snoops.valid) tick();
      repeat (3) begin
        assert(port_out.requester.snoops.valid && port_out.requester.snoops.bits.target_id == target)
          else $fatal(1, "resident snoop changed while stalled");
        tick();
      end
      snoops_ready_in.ready = 1'b1;
      #1;
      assert (port_out.requester.snoops.valid &&
              port_out.requester.snoops.bits.target_id == target &&
              port_out.requester.snoops.bits.flit.opcode == opcode)
        else $fatal(1, "inclusive Home did not invalidate the victim sharer");
      tick();
      snoops_ready_in = '0;
      requester_responses_in.bits = '0;
      requester_responses_in.bits.opcode = SNP_RESP;
      requester_responses_in.bits.src_id = target;
      requester_responses_in.bits.tgt_id = HOME_ID;
      requester_responses_in.bits.resp = state;
      requester_responses_in.bits.resp_err = error;
      requester_responses_in.valid = 1'b1;
      #1;
      assert (port_out.requester.requester_responses.ready)
        else $fatal(1, "inclusive Home did not accept a clean snoop response");
      tick();
      requester_responses_in = '0;
    end
  endtask

  task automatic dirty_snoop(input logic [6:0] target,
                             input logic [7:0] payload_base,
                             input logic [4:0] opcode = SNP_CLEAN_INVALID,
                             input logic [2:0] state = 3'b100,
                             input bit early_error = 0);
    begin
      while (!port_out.requester.snoops.valid) tick();
      snoops_ready_in.ready = 1'b1;
      #1;
      assert (port_out.requester.snoops.valid &&
              port_out.requester.snoops.bits.target_id == target &&
              port_out.requester.snoops.bits.flit.opcode == opcode)
        else $fatal(1, "inclusive Home did not invalidate the dirty victim sharer");
      tick();
      snoops_ready_in = '0;
      for (int packet = 0; packet < 4; packet++) begin
        request_data_in.bits = '0;
        request_data_in.bits.opcode = SNP_RESP_DATA_PTL;
        request_data_in.bits.src_id = target;
        request_data_in.bits.tgt_id = HOME_ID;
        request_data_in.bits.data_id = packet[1:0];
        request_data_in.bits.byte_enable = SNOOP_MASKS[packet * 16 +: 16];
        request_data_in.bits.resp = state;
        request_data_in.bits.resp_err = early_error && packet == 0 ? 2'b10 : 0;
        request_data_in.bits.data = {16{payload_base + packet[7:0]}};
        request_data_in.valid = 1'b1;
        #1;
        assert (port_out.requester.request_data.ready)
          else $fatal(1, "inclusive Home did not accept dirty victim data");
        tick();
        request_data_in = '0;
        if (packet != 3) repeat (2) begin
          assert(!port_out.requester.snoops.valid && !port_out.requester.response_data.valid && !port_out.requester.requests.ready)
            else $fatal(1, "Home retired an incomplete dirty intervention");
          tick();
        end
      end
    end
  endtask

  task automatic finish_cached(input logic [1:0] error = 0);
    while (!port_out.requester.response_data.valid) begin
      assert(!port_out.requester.snoops.valid && !port_out.subordinate.req.valid)
        else $fatal(1, "LLC hit generated an unnecessary snoop or refill");
      tick();
    end
    for (int packet = 0; packet < 4; packet++)
      accept_cached_packet(2'(packet), expected_line[packet], error);
  endtask

  task automatic accept_victim_packet(input logic [1:0] packet_id,
                                      input logic [127:0] payload);
    CHIDatFlit expected_packet;
    begin
      expected_packet = '0;
      expected_packet.data = payload;
      expected_packet.byte_enable = '1;
      expected_packet.data_id = packet_id;
      expected_packet.trace_tag = active_request.trace_tag;
      expected_packet.qos = active_request.qos;
      expected_packet.opcode = NON_COPY_BACK_WRITE_DATA;
      expected_packet.dbid_or_mecid = {4'b0, MEMORY_DBID};
      expected_packet.home_nid_or_pbha_or_mismatched_mecid = HOME_ID;
      expected_packet.src_id = HOME_ID;
      expected_packet.tgt_id = MEMORY_ID;
      expected_packet.txn_id = MEMORY_DBID;
      subordinate_data_ready_in.ready = 1'b1;
      #1;
      assert(port_out.subordinate.dat.request.bits === expected_packet) else $fatal(1, "complete victim DAT mismatch");
      assert (port_out.subordinate.dat.request.valid &&
              port_out.subordinate.dat.request.bits.opcode == NON_COPY_BACK_WRITE_DATA &&
              port_out.subordinate.dat.request.bits.txn_id == MEMORY_DBID &&
              port_out.subordinate.dat.request.bits.data_id == packet_id &&
              port_out.subordinate.dat.request.bits.data == payload)
        else $fatal(1, "inclusive Home wrote back incorrect victim data");
      tick();
      subordinate_data_ready_in = '0;
    end
  endtask

  initial begin
    identity = '{home_node_id: HOME_ID,
                 subordinate_node_id: MEMORY_ID,
                 service_base: LINE0};
    requester_requests_in = '0;
    requester_responses_in = '0;
    request_data_in = '0;
    requester_responses_ready_in = '0;
    response_data_ready_in = '0;
    snoops_ready_in = '0;
    subordinate_responses_in = '0;
    subordinate_requests_ready_in = '0;
    subordinate_data_ready_in = '0;
    subordinate_data_in = '0;
    tick();
    reset = 1'b0;

    requester_requests_in.bits = '0;
    requester_requests_in.bits.src_id = DATA_ID;
    requester_requests_in.bits.tgt_id = HOME_ID;
    requester_requests_in.bits.opcode = WRITE_UNIQUE_PTL;
    requester_requests_in.bits.address = LINE0;
    requester_requests_in.bits.size_or_num_req = 6'd2;
    requester_requests_in.bits.return_nid_or_stash_nid_or_data_target = DATA_ID;
    requester_requests_in.valid = 1'b1;
    #1;
    assert (port_out.requester.requests.ready)
      else $fatal(1, "inclusive Home did not accept a coherent partial write");
    tick();
    requester_requests_in = '0;
    requester_responses_ready_in.ready = 1'b1;
    #1;
    assert (port_out.requester.responses.valid &&
            port_out.requester.responses.bits.opcode == DBID_RESP &&
            port_out.requester.responses.bits.tgt_id == DATA_ID)
      else $fatal(1, "inclusive Home misclassified a coherent partial write");
    tick();
    requester_responses_ready_in = '0;
    reset = 1'b1;
    tick();
    reset = 1'b0;

    send_request(LINE0, READ_NO_SNP);
    tick();
    fill_and_return(LINE0, 8'h10);

    send_request(LINE0, READ_NO_SNP);
    tick();
    tick();
    tick();
    accept_cached_packet(2'd0, 128'h10);
    accept_cached_packet(2'd1, 128'h11);
    accept_cached_packet(2'd2, 128'h12);
    accept_cached_packet(2'd3, 128'h13);
    assert (!port_out.subordinate.req.valid)
      else $fatal(1, "inclusive Home missed on a resident line");

    // RN-I subline responses retain physical line positions, just like RN-F.
    send_request(LINE0 + 44'd16, READ_NO_SNP, 6'd4);
    repeat (3) tick();
    accept_cached_packet(2'd1, 128'h11);
    send_request(LINE0 + 44'd32, READ_NO_SNP, 6'd5);
    repeat (3) tick();
    accept_cached_packet(2'd2, 128'h12);
    accept_cached_packet(2'd3, 128'h13);

    send_request(LINE0, WRITE_NO_SNP_FULL);
    requester_responses_ready_in.ready = 1'b1;
    #1;
    assert (port_out.requester.responses.valid &&
            port_out.requester.responses.bits.opcode == DBID_RESP)
      else $fatal(1, "inclusive Home did not return its write DBID");
    tick();
    requester_responses_ready_in = '0;
    send_write_packet(2'd2, 128'h82);
    send_write_packet(2'd0, 128'h80);
    send_write_packet(2'd3, 128'h83);
    send_write_packet(2'd1, 128'h81);
    tick();
    tick();
    tick();
    tick();
    requester_responses_ready_in.ready = 1'b1;
    #1;
    assert (port_out.requester.responses.valid &&
            port_out.requester.responses.bits.opcode == COMP)
      else $fatal(1, "inclusive Home did not complete the cached write");
    tick();
    requester_responses_ready_in = '0;

    send_request(LINE1, READ_NO_SNP);
    tick();
    fill_and_return(LINE1, 8'h20);

    // A sparse subline write must preserve other bytes and all absent packets,
    // despite stale write-buffer slots containing different data from LINE0.
    for (int packet = 0; packet < 4; packet++)
      expected_line[packet] = 128'h20 + 128'(packet);
    send_request(LINE1 + 44'd16, WRITE_NO_SNP_PTL, 6'd4);
    requester_responses_ready_in.ready = 1'b1;
    #1;
    assert (port_out.requester.responses.valid &&
            port_out.requester.responses.bits.opcode == DBID_RESP)
      else $fatal(1, "inclusive Home did not return the partial-write DBID");
    tick();
    requester_responses_ready_in = '0;
    send_write_packet(2'd1, PARTIAL_DATA, PARTIAL_MASK);
    repeat (4) tick();
    requester_responses_ready_in.ready = 1'b1;
    #1;
    assert (port_out.requester.responses.valid &&
            port_out.requester.responses.bits.opcode == COMP)
      else $fatal(1, "inclusive Home did not complete the partial write");
    tick();
    requester_responses_ready_in = '0;
    for (int byte_index = 0; byte_index < 16; byte_index++)
      if (PARTIAL_MASK[byte_index])
        expected_line[1][byte_index * 8 +: 8] = PARTIAL_DATA[byte_index * 8 +: 8];
    send_request(LINE1, READ_NO_SNP);
    repeat (3) tick();
    for (int packet = 0; packet < 4; packet++)
      accept_cached_packet(packet[1:0], expected_line[packet]);

    for (int packet = 0; packet < 4; packet++)
      expected_line[packet] = 128'h80 + 128'(packet);

    send_request(LINE2, READ_NO_SNP);
    tick();
    fill_and_return(LINE2, 8'h30);

    // Establish the dirty responder's ownership through a real grant. The
    // instruction endpoint never acquired this line and must not be probed.
    send_request(LINE0, 7'h07, 6'd6, DATA_ID);
    repeat (3) tick();
    for (int packet = 0; packet < 4; packet++)
      accept_cached_packet(packet[1:0], expected_line[packet]);
    send_request(LINE3, READ_NO_SNP);
    tick();
    dirty_snoop(DATA_ID, 8'ha0);
    // Compare the complete written-back line against an independent byte model.
    for (int packet = 0; packet < 4; packet++)
      for (int byte_index = 0; byte_index < 16; byte_index++)
        if (SNOOP_MASKS[packet * 16 + byte_index])
          expected_line[packet][byte_index * 8 +: 8] = 8'ha0 + 8'(packet);
    tick();
    tick();
    accept_memory_request(LINE0, WRITE_NO_SNP_FULL);
    subordinate_responses_in.bits = '0;
    subordinate_responses_in.bits.opcode = DBID_RESP;
    subordinate_responses_in.bits.src_id = MEMORY_ID;
    subordinate_responses_in.bits.tgt_id = HOME_ID;
    subordinate_responses_in.bits.dbid_or_group_id = MEMORY_DBID;
    subordinate_responses_in.valid = 1'b1;
    #1;
    assert (port_out.subordinate.rsp.ready)
      else $fatal(1, "inclusive Home did not accept the victim DBID");
    tick();
    subordinate_responses_in = '0;
    for (int packet = 0; packet < 4; packet++)
      accept_victim_packet(packet[1:0], expected_line[packet]);
    subordinate_responses_in.bits = '0;
    subordinate_responses_in.bits.opcode = COMP;
    subordinate_responses_in.bits.src_id = MEMORY_ID;
    subordinate_responses_in.bits.tgt_id = HOME_ID;
    subordinate_responses_in.valid = 1'b1;
    #1;
    assert (port_out.subordinate.rsp.ready)
      else $fatal(1, "inclusive Home did not accept victim completion");
    tick();
    subordinate_responses_in = '0;
    accept_memory_request(LINE3, READ_NO_SNP);
    return_fill_packet(2'd0, 8'h40);
    return_fill_packet(2'd1, 8'h41);
    return_fill_packet(2'd2, 8'h42);
    return_fill_packet(2'd3, 8'h43);
    accept_cached_packet(2'd0, 128'h40);
    accept_cached_packet(2'd1, 128'h41);
    accept_cached_packet(2'd2, 128'h42);
    accept_cached_packet(2'd3, 128'h43);

    // ReadOnce must preserve an early-beat error and must not cache a failed fill.
    reset = 1; tick(); reset = 0;
    send_request(LINE0, 7'h03);
    tick();
    accept_memory_request(LINE0, READ_NO_SNP);
    for (int packet = 0; packet < 4; packet++)
      return_fill_packet(2'(packet), 8'(packet), packet == 0 ? 2'b10 : 2'b00);
    for (int packet = 0; packet < 4; packet++)
      accept_cached_packet(2'(packet), 128'(packet), 2'b10);
    send_request(LINE0, 7'h03);
    tick();
    fill_and_return(LINE0, 8'h50);

    // Snapshot readers do not acquire residency. Repeated polls stay in LLC.
    for (int packet = 0; packet < 4; packet++) expected_line[packet] = 128'h50 + 128'(packet);
    repeat (4) begin send_request(LINE0, 7'h03); finish_cached(); end

    // A coherent partial write is not a cached-copy grant to its sender.
    send_request(LINE0, WRITE_UNIQUE_PTL, 6'd4, DATA_ID);
    requester_responses_ready_in.ready = 1;
    tick(); requester_responses_ready_in = '0;
    request_data_in = '0; request_data_in.valid = 1;
    request_data_in.bits.opcode = NON_COPY_BACK_WRITE_DATA;
    request_data_in.bits.src_id = DATA_ID; request_data_in.bits.tgt_id = HOME_ID;
    request_data_in.bits.byte_enable = '1; request_data_in.bits.data = 128'h60;
    tick(); request_data_in = '0;
    while (!port_out.requester.responses.valid) begin
      assert(!port_out.requester.snoops.valid) else $fatal(1, "write probed a nonresident");
      tick();
    end
    requester_responses_ready_in.ready = 1; tick(); requester_responses_ready_in = '0;
    expected_line[0] = 128'h60;
    send_request(LINE0, 7'h02, 6'd6, INSTRUCTION_ID); finish_cached();

    // Track both shared copies; retain a responder that reports SharedClean.
    send_request(LINE0, 7'h02, 6'd6, DATA_ID);
    clean_snoop(INSTRUCTION_ID, 5'h08, 3'd1); finish_cached();
    send_request(LINE0, 7'h03);
    clean_snoop(INSTRUCTION_ID, 5'h03, 3'd1);
    clean_snoop(DATA_ID, 5'h03, 3'd1); finish_cached();

    // A silent clean eviction leaves one stale positive, then Invalid clears it.
    send_request(LINE0, 7'h03);
    clean_snoop(INSTRUCTION_ID, 5'h03, 3'd0);
    clean_snoop(DATA_ID, 5'h03, 3'd1); finish_cached();
    send_request(LINE0, 7'h03);
    clean_snoop(DATA_ID, 5'h03, 3'd1); finish_cached();

    // Reestablish sharing, then invalidate only the other resident for Unique.
    send_request(LINE0, 7'h02, 6'd6, INSTRUCTION_ID);
    clean_snoop(DATA_ID, 5'h08, 3'd1); finish_cached();
    send_request(LINE0, 7'h07, 6'd6, DATA_ID);
    clean_snoop(INSTRUCTION_ID, 5'h07, 3'd0); finish_cached();
    send_request(LINE0, 7'h03);
    clean_snoop(DATA_ID, 5'h03, 3'd2); finish_cached();

    // A failed Invalid response is not proof of absence and grants no new copy.
    send_request(LINE0, 7'h02, 6'd6, INSTRUCTION_ID);
    clean_snoop(DATA_ID, 5'h08, 3'd0, 2'd2); finish_cached(2'd2);
    send_request(LINE0, 7'h03);
    clean_snoop(DATA_ID, 5'h03, 3'd2); finish_cached();

    // Preserve the resident through an early errored dirty packet, even when
    // the final packet reports success/Invalid. No requester is granted a copy.
    send_request(LINE0, 7'h02, 6'd6, INSTRUCTION_ID);
    dirty_snoop(DATA_ID, 8'hb0, 5'h08, 3'b100, 1);
    for (int packet = 0; packet < 4; packet++)
      for (int b = 0; b < 16; b++)
        if (SNOOP_MASKS[packet*16+b]) expected_line[packet][b*8+:8] = 8'hb0 + 8'(packet);
    finish_cached(2'd2);
    send_request(LINE0, 7'h03);
    dirty_snoop(DATA_ID, 8'hc0, 5'h03, 3'b100);
    for (int packet = 0; packet < 4; packet++)
      for (int b = 0; b < 16; b++)
        if (SNOOP_MASKS[packet*16+b]) expected_line[packet][b*8+:8] = 8'hc0 + 8'(packet);
    finish_cached();
    send_request(LINE0, 7'h03); finish_cached();

    // The complete grant is recorded before a delayed CompAck releases Home.
    send_request(LINE0, 7'h02, 6'd6, INSTRUCTION_ID, 1);
    finish_cached();
    repeat (5) begin
      assert(!port_out.requester.requests.ready) else $fatal(1, "Home released grant before CompAck");
      tick();
    end
    requester_responses_in = '0; requester_responses_in.valid = 1;
    requester_responses_in.bits.opcode = 5'h02;
    requester_responses_in.bits.src_id = INSTRUCTION_ID;
    requester_responses_in.bits.tgt_id = HOME_ID;
    tick(); requester_responses_in = '0;
    send_request(LINE0, 7'h03);
    clean_snoop(INSTRUCTION_ID, 5'h03, 3'd1); finish_cached();

    // Failed victim invalidation must retain the old tag and residency, not
    // recycle its way for an unrelated requested line or emit a refill.
    send_request(LINE2, READ_NO_SNP); tick(); fill_and_return(LINE2, 8'h70);
    send_request(LINE3, READ_NO_SNP);
    clean_snoop(INSTRUCTION_ID, SNP_CLEAN_INVALID, 3'd0, 2'd2);
    finish_cached(2'd2);
    send_request(LINE0, 7'h03);
    clean_snoop(INSTRUCTION_ID, 5'h03, 3'd0); finish_cached();

    $display("CHI inclusive Home residency, response errors, and storage simulation passed");
    $finish;
  end
endmodule
