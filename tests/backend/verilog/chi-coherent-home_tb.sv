// Simulates serialized coherent reads, dirty intervention, and invalidation through CHIHNF.
module chi_coherent_home_tb;
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
    struct packed {
      ready_t request;
      dat_t response;
    } dat;
  } subordinate_in_t;
  typedef struct packed {
    ready_t rsp;
    req_t req;
    struct packed {
      dat_t request;
      ready_t response;
    } dat;
  } subordinate_out_t;
  typedef struct packed {
    requester_in_t requester;
    subordinate_in_t subordinate;
  } hnf_in_t;
  typedef struct packed {
    requester_out_t requester;
    subordinate_out_t subordinate;
  } hnf_out_t;

  localparam logic [6:0] READ_CLEAN = 7'h02;
  localparam logic [6:0] READ_NO_SNP = 7'h04;
  localparam logic [6:0] WRITE_UNIQUE_PTL = 7'h18;
  localparam logic [6:0] WRITE_NO_SNP_PTL = 7'h1c;
  localparam logic [6:0] WRITE_NO_SNP_FULL = 7'h1d;
  localparam logic [4:0] SNP_RESP = 5'h01;
  localparam logic [4:0] COMP_ACK = 5'h02;
  localparam logic [4:0] COMP = 5'h04;
  localparam logic [4:0] DBID_RESP = 5'h06;
  localparam logic [4:0] SNP_CLEAN_INVALID = 5'h09;
  localparam logic [4:0] SNP_CLEAN_SHARED = 5'h08;
  localparam logic [3:0] SNP_RESP_DATA = 4'h1;
  localparam logic [3:0] NON_COPY_BACK_WRITE_DATA = 4'h3;
  localparam logic [3:0] COMP_DATA = 4'h4;
  localparam logic [6:0] HTIF_ID = 7'h01;
  localparam logic [6:0] INSTRUCTION_ID = 7'h02;
  localparam logic [6:0] DATA_ID = 7'h03;
  localparam logic [6:0] HOME_ID = 7'h05;
  localparam logic [6:0] MEMORY_ID = 7'h09;
  localparam logic [11:0] MEMORY_DBID = 12'h055;
  localparam logic [43:0] LINE_ADDRESS = 44'h080000000;

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

  CHIHNF dut (.*);
  always #5 clock = ~clock;

  task automatic tick;
    begin
      @(posedge clock);
      #1;
    end
  endtask

  task automatic send_request(
    input logic [6:0] source,
    input logic [6:0] opcode,
    input logic [5:0] size,
    input logic expect_comp_ack
  );
    begin
      requester_requests_in.bits = '0;
      requester_requests_in.bits.src_id = source;
      requester_requests_in.bits.tgt_id = HOME_ID;
      requester_requests_in.bits.opcode = opcode;
      requester_requests_in.bits.address = LINE_ADDRESS;
      requester_requests_in.bits.size_or_num_req = size;
      requester_requests_in.bits.return_nid_or_stash_nid_or_data_target = source;
      requester_requests_in.bits.return_txn_id_or_stash_lpid = 12'h000;
      requester_requests_in.bits.txn_id = 12'h000;
      requester_requests_in.bits.exp_comp_ack = expect_comp_ack;
      requester_requests_in.valid = 1'b1;
      #1;
      assert (port_out.requester.requests.ready)
        else $fatal(1, "HN-F did not accept an idle requester");
      tick();
      requester_requests_in = '0;
    end
  endtask

  task automatic accept_subordinate_request(input logic [6:0] opcode);
    begin
      #1;
      assert (port_out.subordinate.req.valid)
        else $fatal(1, "HN-F did not issue the subordinate request");
      assert (port_out.subordinate.req.bits.opcode == opcode &&
              port_out.subordinate.req.bits.src_id == HOME_ID &&
              port_out.subordinate.req.bits.tgt_id == MEMORY_ID &&
              port_out.subordinate.req.bits.txn_id == 12'h000)
        else $fatal(1, "HN-F subordinate request translation was incorrect");
      subordinate_requests_ready_in.ready = 1'b1;
      tick();
      subordinate_requests_ready_in = '0;
    end
  endtask

  task automatic accept_snoop(input logic [6:0] target,
                              input logic [4:0] opcode);
    begin
      snoops_ready_in.ready = 1'b1;
      #1;
      assert (port_out.requester.snoops.valid &&
              port_out.requester.snoops.bits.target_id == target &&
              port_out.requester.snoops.bits.flit.opcode == opcode &&
              port_out.requester.snoops.bits.flit.src_id == HOME_ID)
        else $fatal(1, "HN-F emitted incorrect snoop metadata");
      tick();
      snoops_ready_in = '0;
    end
  endtask

  task automatic service_dirty_snoop_packet(input logic [1:0] packet_id);
    begin
      request_data_in.bits = '0;
      request_data_in.bits.opcode = SNP_RESP_DATA;
      request_data_in.bits.src_id = DATA_ID;
      request_data_in.bits.tgt_id = HOME_ID;
      request_data_in.bits.txn_id = 12'h000;
      request_data_in.bits.data_id = packet_id;
      request_data_in.bits.byte_enable = 16'hffff;
      request_data_in.bits.resp = 3'b100;
      request_data_in.bits.data = {120'h0, 6'h0, packet_id};
      request_data_in.valid = 1'b1;
      #1;
      assert (port_out.requester.request_data.ready)
        else $fatal(1, "HN-F did not accept dirty snoop data");
      tick();
      request_data_in = '0;

      accept_subordinate_request(WRITE_NO_SNP_FULL);
      subordinate_responses_in.bits = '0;
      subordinate_responses_in.bits.opcode = DBID_RESP;
      subordinate_responses_in.bits.src_id = MEMORY_ID;
      subordinate_responses_in.bits.tgt_id = HOME_ID;
      subordinate_responses_in.bits.txn_id = 12'h000;
      subordinate_responses_in.bits.dbid_or_group_id = MEMORY_DBID;
      subordinate_responses_in.valid = 1'b1;
      #1;
      assert (port_out.subordinate.rsp.ready)
        else $fatal(1, "HN-F did not accept dirty-data write DBID");
      tick();
      subordinate_responses_in = '0;

      subordinate_data_ready_in.ready = 1'b1;
      #1;
      assert (port_out.subordinate.dat.request.valid &&
              port_out.subordinate.dat.request.bits.opcode == NON_COPY_BACK_WRITE_DATA &&
              port_out.subordinate.dat.request.bits.data_id == packet_id &&
              port_out.subordinate.dat.request.bits.data == {120'h0, 6'h0, packet_id})
        else $fatal(1, "HN-F did not translate dirty snoop data");
      tick();
      subordinate_data_ready_in = '0;

      subordinate_responses_in.bits = '0;
      subordinate_responses_in.bits.opcode = COMP;
      subordinate_responses_in.bits.src_id = MEMORY_ID;
      subordinate_responses_in.bits.tgt_id = HOME_ID;
      subordinate_responses_in.bits.txn_id = 12'h000;
      subordinate_responses_in.valid = 1'b1;
      #1;
      assert (port_out.subordinate.rsp.ready)
        else $fatal(1, "HN-F did not accept dirty-data write completion");
      tick();
      subordinate_responses_in = '0;
    end
  endtask

  task automatic return_read_packet(
    input logic [1:0] packet_id,
    input logic [6:0] target_id,
    input logic [2:0] response
  );
    begin
      subordinate_data_in.bits = '0;
      subordinate_data_in.bits.opcode = COMP_DATA;
      subordinate_data_in.bits.src_id = MEMORY_ID;
      subordinate_data_in.bits.tgt_id = HOME_ID;
      subordinate_data_in.bits.txn_id = 12'h000;
      subordinate_data_in.bits.data_id = packet_id;
      subordinate_data_in.bits.byte_enable = 16'hffff;
      subordinate_data_in.bits.data = {120'h0, 6'h0, packet_id};
      subordinate_data_in.valid = 1'b1;
      response_data_ready_in.ready = 1'b1;
      #1;
      assert (port_out.subordinate.dat.response.ready &&
              port_out.requester.response_data.valid)
        else $fatal(1, "HN-F did not forward a read packet");
      assert (port_out.requester.response_data.bits.src_id == HOME_ID &&
              port_out.requester.response_data.bits.tgt_id == target_id &&
              port_out.requester.response_data.bits.txn_id == 12'h000 &&
              port_out.requester.response_data.bits.data_id == packet_id &&
              port_out.requester.response_data.bits.resp == response)
        else $fatal(1, "HN-F coherent read response metadata was incorrect");
      tick();
      subordinate_data_in = '0;
      response_data_ready_in = '0;
    end
  endtask

  task automatic send_requester_write_packet(input logic [1:0] packet_id);
    begin
      request_data_in.bits = '0;
      request_data_in.bits.opcode = NON_COPY_BACK_WRITE_DATA;
      request_data_in.bits.src_id = HTIF_ID;
      request_data_in.bits.tgt_id = HOME_ID;
      request_data_in.bits.txn_id = 12'h000;
      request_data_in.bits.data_id = packet_id;
      request_data_in.bits.byte_enable = 16'hffff;
      request_data_in.bits.data = {120'h0, 6'h0, packet_id};
      request_data_in.valid = 1'b1;
      #1;
      assert (port_out.requester.request_data.ready)
        else $fatal(1, "HN-F did not collect a requester write packet");
      tick();
      request_data_in = '0;
    end
  endtask

  task automatic accept_subordinate_write_packet(input logic [1:0] packet_id);
    begin
      subordinate_data_ready_in.ready = 1'b1;
      #1;
      assert (port_out.subordinate.dat.request.valid &&
              port_out.subordinate.dat.request.bits.opcode == NON_COPY_BACK_WRITE_DATA &&
              port_out.subordinate.dat.request.bits.txn_id == MEMORY_DBID &&
              port_out.subordinate.dat.request.bits.data_id == packet_id &&
              port_out.subordinate.dat.request.bits.data == {120'h0, 6'h0, packet_id})
        else $fatal(1, "HN-F did not forward the expected write packet");
      tick();
      subordinate_data_ready_in = '0;
    end
  endtask

  initial begin
    identity = '{home_node_id: HOME_ID,
                 subordinate_node_id: MEMORY_ID,
                 service_base: LINE_ADDRESS};
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

    send_request(INSTRUCTION_ID, READ_CLEAN, 6'd6, 1'b1);
    accept_snoop(DATA_ID, SNP_CLEAN_SHARED);
    service_dirty_snoop_packet(2'd0);
    service_dirty_snoop_packet(2'd1);
    service_dirty_snoop_packet(2'd2);
    service_dirty_snoop_packet(2'd3);
    tick();
    accept_subordinate_request(READ_NO_SNP);
    return_read_packet(2'd0, INSTRUCTION_ID, 3'b001);
    return_read_packet(2'd1, INSTRUCTION_ID, 3'b001);
    return_read_packet(2'd2, INSTRUCTION_ID, 3'b001);
    return_read_packet(2'd3, INSTRUCTION_ID, 3'b001);
    assert (!port_out.requester.requests.ready)
      else $fatal(1, "HN-F released a coherent read before CompAck");
    requester_responses_in.bits = '0;
    requester_responses_in.bits.opcode = COMP_ACK;
    requester_responses_in.bits.src_id = INSTRUCTION_ID;
    requester_responses_in.bits.tgt_id = HOME_ID;
    requester_responses_in.bits.txn_id = 12'h000;
    requester_responses_in.valid = 1'b1;
    #1;
    assert (port_out.requester.requester_responses.ready)
      else $fatal(1, "HN-F did not accept coherent CompAck");
    tick();
    requester_responses_in = '0;
    assert (port_out.requester.requests.ready)
      else $fatal(1, "HN-F did not retire the coherent read");

    // An RN-I snapshot must consult both coherent owners without receiving ownership.
    send_request(HTIF_ID, 7'h03, 6'd6, 1'b0);
    accept_snoop(INSTRUCTION_ID, 5'h03);
    requester_responses_in.bits = '0;
    requester_responses_in.bits.opcode = SNP_RESP;
    requester_responses_in.bits.src_id = INSTRUCTION_ID;
    requester_responses_in.bits.tgt_id = HOME_ID;
    requester_responses_in.bits.resp = 3'b010; // A clean owner may remain Unique.
    requester_responses_in.valid = 1;
    tick();
    requester_responses_in = '0;
    accept_snoop(DATA_ID, 5'h03);
    for (int packet = 0; packet < 4; packet++) service_dirty_snoop_packet(2'(packet));
    tick();
    accept_subordinate_request(READ_NO_SNP);
    for (int packet = 0; packet < 4; packet++) return_read_packet(2'(packet), HTIF_ID, 3'b000);
    assert (port_out.requester.requests.ready) else $fatal(1, "ReadOnce did not retire");

    send_request(HTIF_ID, READ_NO_SNP, 6'd2, 1'b0);
    accept_subordinate_request(READ_NO_SNP);
    return_read_packet(2'd0, HTIF_ID, 3'b000);
    assert (port_out.requester.requests.ready)
      else $fatal(1, "HN-F did not retire the RN-I read without CompAck");

    send_request(DATA_ID, WRITE_UNIQUE_PTL, 6'd3, 1'b0);
    requester_responses_ready_in.ready = 1'b1;
    #1;
    assert (port_out.requester.responses.valid &&
            port_out.requester.responses.bits.opcode == DBID_RESP &&
            port_out.requester.responses.bits.tgt_id == DATA_ID)
      else $fatal(1, "HN-F did not allocate its requester DBID");
    tick();
    requester_responses_ready_in = '0;

    request_data_in.bits = '0;
    request_data_in.bits.opcode = NON_COPY_BACK_WRITE_DATA;
    request_data_in.bits.src_id = DATA_ID;
    request_data_in.bits.tgt_id = HOME_ID;
    request_data_in.bits.txn_id = 12'h000;
    request_data_in.bits.byte_enable = 16'h00ff;
    request_data_in.bits.data = 128'h1122334455667788;
    request_data_in.valid = 1'b1;
    #1;
    assert (port_out.requester.request_data.ready)
      else $fatal(1, "HN-F did not collect requester write data");
    tick();
    request_data_in = '0;

    accept_snoop(INSTRUCTION_ID, SNP_CLEAN_INVALID);

    requester_responses_in.bits = '0;
    requester_responses_in.bits.opcode = SNP_RESP;
    requester_responses_in.bits.src_id = INSTRUCTION_ID;
    requester_responses_in.bits.tgt_id = HOME_ID;
    requester_responses_in.bits.txn_id = 12'h000;
    requester_responses_in.valid = 1'b1;
    #1;
    assert (port_out.requester.requester_responses.ready)
      else $fatal(1, "HN-F did not accept the snoop response");
    tick();
    requester_responses_in = '0;
    tick();

    accept_subordinate_request(WRITE_NO_SNP_PTL);
    subordinate_responses_in.bits = '0;
    subordinate_responses_in.bits.opcode = DBID_RESP;
    subordinate_responses_in.bits.src_id = MEMORY_ID;
    subordinate_responses_in.bits.tgt_id = HOME_ID;
    subordinate_responses_in.bits.txn_id = 12'h000;
    subordinate_responses_in.bits.dbid_or_group_id = MEMORY_DBID;
    subordinate_responses_in.valid = 1'b1;
    #1;
    assert (port_out.subordinate.rsp.ready)
      else $fatal(1, "HN-F did not accept the subordinate DBID");
    tick();
    subordinate_responses_in = '0;

    subordinate_data_ready_in.ready = 1'b1;
    #1;
    assert (port_out.subordinate.dat.request.valid &&
            port_out.subordinate.dat.request.bits.src_id == HOME_ID &&
            port_out.subordinate.dat.request.bits.tgt_id == MEMORY_ID &&
            port_out.subordinate.dat.request.bits.txn_id == MEMORY_DBID &&
            port_out.subordinate.dat.request.bits.data == 128'h1122334455667788)
      else $fatal(1, "HN-F subordinate write-data translation was incorrect");
    tick();
    subordinate_data_ready_in = '0;

    subordinate_responses_in.bits = '0;
    subordinate_responses_in.bits.opcode = COMP;
    subordinate_responses_in.bits.src_id = MEMORY_ID;
    subordinate_responses_in.bits.tgt_id = HOME_ID;
    subordinate_responses_in.bits.txn_id = 12'h000;
    subordinate_responses_in.bits.dbid_or_group_id = MEMORY_DBID;
    subordinate_responses_in.valid = 1'b1;
    #1;
    assert (port_out.subordinate.rsp.ready)
      else $fatal(1, "HN-F did not accept subordinate completion");
    tick();
    subordinate_responses_in = '0;

    requester_responses_ready_in.ready = 1'b1;
    #1;
    assert (port_out.requester.responses.valid &&
            port_out.requester.responses.bits.opcode == COMP &&
            port_out.requester.responses.bits.src_id == HOME_ID &&
            port_out.requester.responses.bits.tgt_id == DATA_ID)
      else $fatal(1, "HN-F did not complete the requester write");
    tick();
    assert (port_out.requester.requests.ready)
      else $fatal(1, "HN-F did not retire the write");
    requester_responses_ready_in = '0;

    send_request(HTIF_ID, WRITE_NO_SNP_FULL, 6'd6, 1'b0);
    requester_responses_ready_in.ready = 1'b1;
    #1;
    assert (port_out.requester.responses.valid &&
            port_out.requester.responses.bits.opcode == DBID_RESP)
      else $fatal(1, "HN-F did not allocate the line-write requester DBID");
    tick();
    requester_responses_ready_in = '0;

    send_requester_write_packet(2'd2);
    send_requester_write_packet(2'd0);
    send_requester_write_packet(2'd3);
    send_requester_write_packet(2'd1);
    tick();
    accept_subordinate_request(WRITE_NO_SNP_FULL);

    subordinate_responses_in.bits = '0;
    subordinate_responses_in.bits.opcode = DBID_RESP;
    subordinate_responses_in.bits.src_id = MEMORY_ID;
    subordinate_responses_in.bits.tgt_id = HOME_ID;
    subordinate_responses_in.bits.txn_id = 12'h000;
    subordinate_responses_in.bits.dbid_or_group_id = MEMORY_DBID;
    subordinate_responses_in.valid = 1'b1;
    #1;
    assert (port_out.subordinate.rsp.ready)
      else $fatal(1, "HN-F did not accept the line-write subordinate DBID");
    tick();
    subordinate_responses_in = '0;

    accept_subordinate_write_packet(2'd0);
    accept_subordinate_write_packet(2'd1);
    accept_subordinate_write_packet(2'd2);
    accept_subordinate_write_packet(2'd3);

    subordinate_responses_in.bits = '0;
    subordinate_responses_in.bits.opcode = COMP;
    subordinate_responses_in.bits.src_id = MEMORY_ID;
    subordinate_responses_in.bits.tgt_id = HOME_ID;
    subordinate_responses_in.bits.txn_id = 12'h000;
    subordinate_responses_in.bits.dbid_or_group_id = MEMORY_DBID;
    subordinate_responses_in.valid = 1'b1;
    #1;
    assert (port_out.subordinate.rsp.ready)
      else $fatal(1, "HN-F did not accept the line-write completion");
    tick();
    subordinate_responses_in = '0;

    requester_responses_ready_in.ready = 1'b1;
    #1;
    assert (port_out.requester.responses.valid &&
            port_out.requester.responses.bits.opcode == COMP)
      else $fatal(1, "HN-F did not complete the line write");
    tick();
    assert (port_out.requester.requests.ready)
      else $fatal(1, "HN-F did not retire the line write");

    $display("CHI serialized HN-F simulation passed");
    $finish;
  end
endmodule
