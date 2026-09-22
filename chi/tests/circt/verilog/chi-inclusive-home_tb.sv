// Checks inclusive residency, copyback ownership, races, and complete cache-line packets.
// SPDX-License-Identifier: Apache-2.0
module chi_inclusive_home_tb #(parameter int INVALID_CASE = 0);
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
  localparam logic [6:0] READ_ONCE = 7'h03;
  localparam logic [6:0] WRITE_NO_SNP_FULL = 7'h1d;
  localparam logic [6:0] WRITE_NO_SNP_PTL = 7'h1c;
  localparam logic [6:0] WRITE_UNIQUE_PTL = 7'h18;
  localparam logic [4:0] SNP_RESP = 5'h01;
  localparam logic [4:0] COMP_ACK = 5'h02;
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
  logic [11:0] response_dbid;
  logic [11:0] first_comp_ack_dbid;
  logic [11:0] second_comp_ack_dbid;
  logic [11:0] first_memory_txn;
  logic [11:0] second_memory_txn;
  logic [11:0] victim_memory_txn;
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

`ifdef CHI_HOME_TRACE
  EventHome dut (.*);
`else
  CHIInclusiveHNF dut (.*);
`endif
`ifdef CHI_HOME_TRACE
  import "DPI-C" function void event_home_bind();
  import "DPI-C" function void event_home_sample(input int unsigned reset,
    input int unsigned request_fire, input longint unsigned address,
    input int unsigned request_opcode, request_txn, request_src,
    input int unsigned response_fire, response_opcode, response_txn, response_tgt,
    input int unsigned data_fire, data_opcode, data_txn, data_tgt, data_id,
    input int unsigned backing_fire, output_stalled);
  import "DPI-C" function void event_home_check();
  import "DPI-C" function void event_home_finish();
  initial event_home_bind();
  always @(posedge clock) begin
    event_home_sample(32'(reset), 32'(requester_requests_in.valid && port_out.requester.requests.ready),
      64'(requester_requests_in.bits.address), 32'(requester_requests_in.bits.opcode),
      32'(requester_requests_in.bits.txn_id), 32'(requester_requests_in.bits.src_id),
      32'(port_out.requester.responses.valid && requester_responses_ready_in.ready),
      32'(port_out.requester.responses.bits.opcode), 32'(port_out.requester.responses.bits.txn_id), 32'(port_out.requester.responses.bits.tgt_id),
      32'(port_out.requester.response_data.valid && response_data_ready_in.ready),
      32'(port_out.requester.response_data.bits.opcode), 32'(port_out.requester.response_data.bits.txn_id),
      32'(port_out.requester.response_data.bits.tgt_id), 32'(port_out.requester.response_data.bits.data_id),
      32'(port_out.subordinate.req.valid && subordinate_requests_ready_in.ready),
      32'((port_out.requester.responses.valid && !requester_responses_ready_in.ready) ||
          (port_out.requester.response_data.valid && !response_data_ready_in.ready)));
    #0.5;
    event_home_check();
  end
`endif
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
                              input bit exp_comp_ack = 0,
                              input bit allocate = 0,
                              input logic [11:0] request_txn = 12'h000,
                              input logic [11:0] return_txn = 12'h654);
    begin
      requester_requests_in.bits = '0;
      requester_requests_in.bits.src_id = source;
      requester_requests_in.bits.tgt_id = HOME_ID;
      requester_requests_in.bits.opcode = opcode;
      requester_requests_in.bits.address = address;
      requester_requests_in.bits.size_or_num_req = request_size;
      requester_requests_in.bits.txn_id = request_txn;
      requester_requests_in.bits.return_nid_or_stash_nid_or_data_target = source;
      requester_requests_in.bits.return_txn_id_or_stash_lpid = return_txn;
      requester_requests_in.bits.trace_tag = 1;
      requester_requests_in.bits.exp_comp_ack = exp_comp_ack;
      requester_requests_in.bits.mem_attr.allocate = allocate;
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
      while (!port_out.subordinate.req.valid) tick();
      #1;
      assert (port_out.subordinate.req.valid &&
              port_out.subordinate.req.bits.address == address &&
              port_out.subordinate.req.bits.size_or_num_req == 6'd6 &&
              port_out.subordinate.req.bits.opcode == opcode)
        else $fatal(1, "inclusive Home issued an incorrect memory request");
      subordinate_requests_ready_in.ready = 1'b1;
      tick();
      subordinate_requests_ready_in = '0;
    end
  endtask

  task automatic accept_memory_request_transaction(input logic [43:0] address,
                                                   input logic [6:0] opcode,
                                                   output logic [11:0] transaction);
    begin
      while (!port_out.subordinate.req.valid) tick();
      #1;
      assert (port_out.subordinate.req.bits.address == address &&
              port_out.subordinate.req.bits.size_or_num_req == 6'd6 &&
              port_out.subordinate.req.bits.opcode == opcode)
        else $fatal(1, "inclusive Home issued an incorrect identified memory request");
      transaction = port_out.subordinate.req.bits.txn_id;
      subordinate_requests_ready_in.ready = 1'b1;
      tick();
      subordinate_requests_ready_in = '0;
    end
  endtask

  task automatic accept_memory_request_slot(input logic [43:0] address,
                                            output logic [11:0] transaction);
    begin
      while (!port_out.subordinate.req.valid) tick();
      #1;
      assert (port_out.subordinate.req.bits.address == address &&
              port_out.subordinate.req.bits.opcode == READ_NO_SNP &&
              port_out.subordinate.req.bits.txn_id ==
                port_out.subordinate.req.bits.return_txn_id_or_stash_lpid)
        else $fatal(1, "inclusive Home issued an incorrectly identified memory read");
      transaction = port_out.subordinate.req.bits.txn_id;
      subordinate_requests_ready_in.ready = 1'b1;
      tick();
      subordinate_requests_ready_in = '0;
    end
  endtask

  task automatic return_fill_packet(input logic [1:0] packet_id,
                                    input logic [7:0] payload,
                                    input logic [1:0] error = 0,
                                    input logic [11:0] transaction = 0);
    begin
      subordinate_data_in.bits = '0;
      subordinate_data_in.bits.opcode = COMP_DATA;
      subordinate_data_in.bits.resp_err = error;
      subordinate_data_in.bits.src_id = MEMORY_ID;
      subordinate_data_in.bits.tgt_id = HOME_ID;
      subordinate_data_in.bits.txn_id = transaction;
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

  task automatic accept_routed_packet(input logic [11:0] transaction,
                                      input logic [1:0] packet_id,
                                      input logic [7:0] payload,
                                      input logic [6:0] target = HTIF_ID);
    begin
      while (!port_out.requester.response_data.valid) tick();
      #1;
      assert (port_out.requester.response_data.bits.opcode == COMP_DATA &&
              port_out.requester.response_data.bits.txn_id == transaction &&
              port_out.requester.response_data.bits.tgt_id == target &&
              port_out.requester.response_data.bits.data_id == packet_id &&
              port_out.requester.response_data.bits.data == {120'h0, payload})
        else $fatal(1, "inclusive Home routed a completed line to the wrong requester transaction");
      response_data_ready_in.ready = 1'b1;
      tick();
      response_data_ready_in = '0;
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
      if (active_request.exp_comp_ack) begin
        if (packet_id == 0)
          response_dbid = port_out.requester.response_data.bits.dbid_or_mecid[11:0];
        expected_packet.dbid_or_mecid = {4'b0, packet_id == 0 ?
          port_out.requester.response_data.bits.dbid_or_mecid[11:0] : response_dbid};
      end
      if (error == 0 && (active_request.opcode == 7'h02 || active_request.opcode == 7'h07)) expected_packet.resp = active_request.opcode == 7'h07 ? 3'd2 : 3'd1;
      response_data_ready_in.ready = 1'b1;
      #1;
      assert(port_out.requester.response_data.bits === expected_packet) else $fatal(1, "complete cached DAT mismatch");
      assert (port_out.requester.response_data.valid &&
              port_out.requester.response_data.bits.data_id == packet_id &&
              port_out.requester.response_data.bits.data == payload &&
              port_out.requester.response_data.bits.resp_err == error)
        else $fatal(1, "inclusive Home returned incorrect cached data packet=%0d actual=%h expected=%h error=%0d",
                    packet_id, port_out.requester.response_data.bits.data, payload,
                    port_out.requester.response_data.bits.resp_err);
      tick();
      response_data_ready_in = '0;
    end
  endtask

  task automatic send_comp_ack(input logic [6:0] source,
                               input logic [11:0] dbid);
    begin
      requester_responses_in = '0;
      requester_responses_in.bits.opcode = COMP_ACK;
      requester_responses_in.bits.txn_id = dbid;
      requester_responses_in.bits.src_id = source;
      requester_responses_in.bits.tgt_id = HOME_ID;
      requester_responses_in.valid = 1'b1;
      #1;
      assert(port_out.requester.requester_responses.ready)
        else $fatal(1, "inclusive Home did not accept table-owned CompAck");
      tick();
      requester_responses_in = '0;
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
    logic [11:0] snoop_txn_id;
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
      snoop_txn_id = port_out.requester.snoops.bits.flit.txn_id;
      tick();
      snoops_ready_in = '0;
      requester_responses_in.bits = '0;
      requester_responses_in.bits.opcode = SNP_RESP;
      requester_responses_in.bits.src_id = target;
      requester_responses_in.bits.tgt_id = HOME_ID;
      requester_responses_in.bits.txn_id = snoop_txn_id;
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
    logic [11:0] snoop_txn_id;
    begin
      while (!port_out.requester.snoops.valid) tick();
      snoops_ready_in.ready = 1'b1;
      #1;
      assert (port_out.requester.snoops.valid &&
              port_out.requester.snoops.bits.target_id == target &&
              port_out.requester.snoops.bits.flit.opcode == opcode)
        else $fatal(1, "inclusive Home did not invalidate the dirty victim sharer");
      snoop_txn_id = port_out.requester.snoops.bits.flit.txn_id;
      tick();
      snoops_ready_in = '0;
      for (int packet = 0; packet < 4; packet++) begin
        request_data_in.bits = '0;
        request_data_in.bits.opcode = SNP_RESP_DATA_PTL;
        request_data_in.bits.src_id = target;
        request_data_in.bits.tgt_id = HOME_ID;
        request_data_in.bits.txn_id = snoop_txn_id;
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

  task automatic clean_snoops_back_to_back(input logic [6:0] first_target,
                                            input logic [6:0] second_target,
                                            input logic [4:0] opcode,
                                            input logic [2:0] state);
    logic [11:0] first_txn_id;
    logic [11:0] second_txn_id;
    begin
      while (!port_out.requester.snoops.valid) tick();
      snoops_ready_in.ready = 1'b1;
      #1;
      assert(port_out.requester.snoops.bits.target_id == first_target &&
             port_out.requester.snoops.bits.flit.opcode == opcode)
        else $fatal(1, "inclusive Home issued the first pipelined snoop to the wrong resident");
      first_txn_id = port_out.requester.snoops.bits.flit.txn_id;
      tick();
      #1;
      assert(port_out.requester.snoops.valid &&
             port_out.requester.snoops.bits.target_id == second_target &&
             port_out.requester.snoops.bits.flit.opcode == opcode)
        else $fatal(1, "inclusive Home did not issue resident snoops back-to-back");
      second_txn_id = port_out.requester.snoops.bits.flit.txn_id;
      assert(first_txn_id != second_txn_id)
        else $fatal(1, "inclusive Home reused a live snoop transaction ID");
      tick();
      snoops_ready_in = '0;

      requester_responses_in.bits = '0;
      requester_responses_in.bits.opcode = SNP_RESP;
      requester_responses_in.bits.src_id = second_target;
      requester_responses_in.bits.tgt_id = HOME_ID;
      requester_responses_in.bits.txn_id = second_txn_id;
      requester_responses_in.bits.resp = state;
      requester_responses_in.valid = 1'b1;
      #1;
      assert(port_out.requester.requester_responses.ready)
        else $fatal(1, "inclusive Home did not accept the second resident response first");
      tick();

      requester_responses_in.bits.src_id = first_target;
      requester_responses_in.bits.txn_id = first_txn_id;
      #1;
      assert(port_out.requester.requester_responses.ready)
        else $fatal(1, "inclusive Home did not accept the first resident response last");
      tick();
      requester_responses_in = '0;
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

  task automatic copyback(input logic [43:0] address, input logic [2:0] state);
    CHIRspFlit grant;
    begin
      send_request(address, 7'h1b, 6'd6, DATA_ID);
      #1;
      grant = port_out.requester.responses.bits;
      repeat (3) begin
        assert(port_out.requester.responses.valid && grant.opcode == 5'h05 &&
               grant.tgt_id == DATA_ID && grant.resp == 0 &&
               port_out.requester.responses.bits == grant &&
               !port_out.requester.request_data.ready)
          else $fatal(1, "unstable or premature copyback grant");
        tick();
      end
      requester_responses_ready_in.ready = 1; tick(); requester_responses_ready_in = '0;
      // Reverse arrival order exercises DataID assembly, not packet counting.
      for (int packet = 3; packet >= 0; packet--) begin
        request_data_in = '0; request_data_in.valid = 1;
        request_data_in.bits.opcode = 4'h2;
        request_data_in.bits.src_id = DATA_ID;
        request_data_in.bits.tgt_id = HOME_ID;
        request_data_in.bits.txn_id = grant.dbid_or_group_id;
        request_data_in.bits.data_id = 2'(packet);
        request_data_in.bits.resp = state;
        request_data_in.bits.byte_enable = state == 0 ? 0 : '1;
        request_data_in.bits.data = state == 0 ? 0 : 128'hdead0000 + 128'(packet);
        if (INVALID_CASE == 1) request_data_in.bits.byte_enable = 16'hff;
        if (INVALID_CASE == 2 && packet == 2) request_data_in.bits.data_id = 3;
        if (INVALID_CASE == 3 && packet == 2) request_data_in.bits.resp = 3'd7;
        #1;
        assert(port_out.requester.request_data.ready) else $fatal(1, "copyback buffer unavailable");
        tick(); request_data_in = '0;
        repeat (2) begin
          assert(!port_out.requester.responses.valid && !port_out.requester.snoops.valid &&
                 !port_out.subordinate.req.valid) else $fatal(1, "copyback generated extra traffic");
          tick();
        end
      end
      while (!port_out.requester.requests.ready) begin
        assert(!port_out.requester.responses.valid && !port_out.requester.snoops.valid &&
               !port_out.subordinate.req.valid) else $fatal(1, "copyback generated extra traffic");
        tick();
      end
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

`ifndef CHI_HOME_TRACE
    // A cached line in one set completes while a distinct-set miss remains
    // parked on DRAM. A non-final fill beat may arrive while the hit DAT is
    // stalled because it only updates the miss slot's private line buffer.
    send_request(LINE0, READ_ONCE, 6'd6, HTIF_ID, 0, 1);
    tick();
    fill_and_return(LINE0, 8'h50);
    send_request(LINE1, READ_ONCE, 6'd6, HTIF_ID, 0, 0, 12'h030, 12'h330);
    accept_memory_request_slot(LINE1, first_memory_txn);
    requester_requests_in.bits = '0;
    requester_requests_in.bits.src_id = HTIF_ID;
    requester_requests_in.bits.tgt_id = HOME_ID;
    requester_requests_in.bits.opcode = READ_ONCE;
    requester_requests_in.bits.address = LINE1 + 44'h200;
    requester_requests_in.bits.size_or_num_req = 6'd6;
    requester_requests_in.valid = 1'b1;
    #1;
    assert (!port_out.requester.requests.ready)
      else $fatal(1, "inclusive Home admitted a same-set request under miss");
    requester_requests_in = '0;
    send_request(LINE0, READ_ONCE, 6'd6, HTIF_ID, 0, 1, 12'h040, 12'h440);
    while (!port_out.requester.response_data.valid) begin
      assert (!port_out.requester.snoops.valid && !port_out.subordinate.req.valid)
        else $fatal(1, "inclusive Home hit-under-miss generated extra traffic");
      tick();
    end
    subordinate_data_in.bits = '0;
    subordinate_data_in.bits.opcode = COMP_DATA;
    subordinate_data_in.bits.src_id = MEMORY_ID;
    subordinate_data_in.bits.tgt_id = HOME_ID;
    subordinate_data_in.bits.txn_id = first_memory_txn;
    subordinate_data_in.bits.data_id = 0;
    subordinate_data_in.bits.byte_enable = 16'hffff;
    subordinate_data_in.bits.data = 128'h60;
    subordinate_data_in.valid = 1'b1;
    #1;
    assert (port_out.subordinate.dat.response.ready &&
            port_out.requester.response_data.valid &&
            port_out.requester.response_data.bits.txn_id == 12'h440 &&
            port_out.requester.response_data.bits.data_id == 0 &&
            port_out.requester.response_data.bits.data == 128'h50)
      else $fatal(1, "inclusive Home did not overlap a hit with an unrelated fill");
    tick();
    subordinate_data_in = '0;
    for (int packet = 0; packet < 4; packet++)
      accept_cached_packet(2'(packet), 128'h50 + 128'(packet));
    for (int packet = 1; packet < 4; packet++)
      return_fill_packet(2'(packet), 8'h60 + 8'(packet), 0, first_memory_txn);
    for (int packet = 0; packet < 4; packet++)
      accept_routed_packet(12'h330, 2'(packet), 8'h60 + 8'(packet));
    reset = 1'b1;
    tick();
    reset = 1'b0;

    // A fill owned by the second transaction slot must write its own tag,
    // independent of the lookup selector's current or invalid choice.
    send_request(LINE0, READ_ONCE, 6'd6, HTIF_ID, 0, 1, 12'h050, 12'h150);
    accept_memory_request_slot(LINE0, first_memory_txn);
    send_request(LINE1, 7'h07, 6'd6, DATA_ID, 0, 0, 12'h060, 12'h160);
    accept_memory_request_slot(LINE1, second_memory_txn);
    for (int packet = 0; packet < 4; packet++)
      return_fill_packet(2'(packet), 8'h70 + 8'(packet), 0, second_memory_txn);
    for (int packet = 0; packet < 4; packet++)
      accept_routed_packet(12'h160, 2'(packet), 8'h70 + 8'(packet), DATA_ID);
    for (int packet = 0; packet < 4; packet++)
      return_fill_packet(2'(packet), 8'h60 + 8'(packet), 0, first_memory_txn);
    for (int packet = 0; packet < 4; packet++)
      accept_routed_packet(12'h150, 2'(packet), 8'h60 + 8'(packet));
    copyback(LINE1, 3'b110);
    reset = 1'b1;
    tick();
    reset = 1'b0;

    // Distinct sets occupy independent transaction slots, while a request for
    // the first set remains serialized until its owner releases the set.
    send_request(LINE0, READ_ONCE, 6'd6, HTIF_ID, 0, 0, 12'h010, 12'h110);
    requester_requests_in.bits = '0;
    requester_requests_in.bits.src_id = HTIF_ID;
    requester_requests_in.bits.tgt_id = HOME_ID;
    requester_requests_in.bits.opcode = READ_ONCE;
    requester_requests_in.bits.address = LINE2;
    requester_requests_in.bits.size_or_num_req = 6'd6;
    requester_requests_in.bits.return_nid_or_stash_nid_or_data_target = HTIF_ID;
    requester_requests_in.valid = 1'b1;
    #1;
    assert (!port_out.requester.requests.ready)
      else $fatal(1, "inclusive Home admitted a conflicting set transaction");
    requester_requests_in = '0;
    send_request(LINE1, READ_ONCE, 6'd6, HTIF_ID, 0, 0, 12'h020, 12'h220);
    accept_memory_request_slot(LINE0, first_memory_txn);
    accept_memory_request_slot(LINE1, second_memory_txn);
    assert (first_memory_txn != second_memory_txn)
      else $fatal(1, "inclusive Home reused a live transaction slot");
    for (int packet = 0; packet < 4; packet++)
      return_fill_packet(2'(packet), 8'h20 + 8'(packet), 0, second_memory_txn);
    while (!port_out.requester.response_data.valid) tick();
    tick();
    subordinate_data_in.bits = '0;
    subordinate_data_in.bits.opcode = COMP_DATA;
    subordinate_data_in.bits.src_id = MEMORY_ID;
    subordinate_data_in.bits.tgt_id = HOME_ID;
    subordinate_data_in.bits.txn_id = first_memory_txn;
    subordinate_data_in.bits.data_id = 0;
    subordinate_data_in.bits.byte_enable = 16'hffff;
    subordinate_data_in.bits.data = 128'h10;
    subordinate_data_in.valid = 1'b1;
    #1;
    assert (port_out.subordinate.dat.response.ready &&
            port_out.requester.response_data.valid &&
            port_out.requester.response_data.bits.txn_id == 12'h220 &&
            port_out.requester.response_data.bits.data_id == 0 &&
            port_out.requester.response_data.bits.data == 128'h20)
      else $fatal(1, "inclusive Home did not accept an independent fill");
    tick();
    subordinate_data_in = '0;
    repeat (2) begin
      #1;
      assert (port_out.requester.response_data.valid &&
              port_out.requester.response_data.bits.txn_id == 12'h220 &&
              port_out.requester.response_data.bits.data_id == 0 &&
              port_out.requester.response_data.bits.data == 128'h20)
        else $fatal(1, "inclusive Home changed a stalled shared-output owner");
      tick();
    end
    for (int packet = 1; packet < 4; packet++)
      return_fill_packet(2'(packet), 8'h10 + 8'(packet), 0, first_memory_txn);
    // Once both transactions can return DAT, beat-level round-robin service
    // prevents either live slot from monopolizing the requester channel.
    for (int packet = 0; packet < 4; packet++) begin
      accept_routed_packet(12'h220, 2'(packet), 8'h20 + 8'(packet));
      accept_routed_packet(12'h110, 2'(packet), 8'h10 + 8'(packet));
    end
    reset = 1'b1;
    tick();
    reset = 1'b0;
`endif

    // Allocating ReadOnce misses install and subsequently hit.
    send_request(LINE0, READ_ONCE, 6'd6, HTIF_ID, 0, 1);
    tick();
    fill_and_return(LINE0, 8'h08);
    for (int packet = 0; packet < 4; packet++) expected_line[packet] = 128'h08 + 128'(packet);
    send_request(LINE0, READ_ONCE, 6'd6, HTIF_ID, 0, 1);
    finish_cached();

    // Fill the other way and establish an RN-F resident in the first line.
    send_request(LINE2, READ_ONCE, 6'd6, HTIF_ID, 0, 1);
    tick();
    fill_and_return(LINE2, 8'h18);
    send_request(LINE0, 7'h02, 6'd6, DATA_ID);
    finish_cached();

    // A nonallocating hit still observes the tracked RN-F copy.
    send_request(LINE0, READ_ONCE);
    clean_snoop(DATA_ID, 5'h03, 3'd1);
    finish_cached();

    // A nonallocating miss bypasses the full set without snooping or replacing
    // its selected victim, then returns the fetched line directly.
    send_request(LINE3, READ_ONCE);
    while (!port_out.subordinate.req.valid && !port_out.requester.snoops.valid) tick();
    #1;
    assert (!port_out.requester.snoops.valid && port_out.subordinate.req.valid)
      else $fatal(1, "nonallocating ReadOnce miss attempted replacement");
    fill_and_return(LINE3, 8'h28);
    send_request(LINE0, READ_ONCE);
    clean_snoop(DATA_ID, 5'h03, 3'd1);
    finish_cached();

    // The bypassed line was not installed, so another access misses again.
    send_request(LINE3, READ_ONCE);
    tick();
    fill_and_return(LINE3, 8'h38);

    reset = 1'b1;
    tick();
    reset = 1'b0;

    send_request(LINE0, READ_NO_SNP);
    tick();
    fill_and_return(LINE0, 8'h10);

    // An LLC hit with no tracked RN-F resident skips the snoop phases.
    send_request(LINE0, READ_NO_SNP);
    while (!port_out.requester.response_data.valid &&
           !port_out.requester.snoops.valid && !port_out.subordinate.req.valid) tick();
    assert (port_out.requester.response_data.valid &&
            !port_out.requester.snoops.valid &&
            !port_out.subordinate.req.valid)
      else $fatal(1, "inclusive Home did not fast-path a zero-snoop hit");
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
    // Keep LINE0 as the PLRU victim while preserving its dirty resident owner.
    for (int packet = 0; packet < 4; packet++)
      expected_line[packet] = 128'h30 + 128'(packet);
    send_request(LINE2, READ_NO_SNP);
    repeat (3) tick();
    for (int packet = 0; packet < 4; packet++)
      accept_cached_packet(packet[1:0], expected_line[packet]);
    for (int packet = 0; packet < 4; packet++)
      expected_line[packet] = 128'h80 + 128'(packet);
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
    accept_memory_request_transaction(LINE0, WRITE_NO_SNP_FULL, victim_memory_txn);
    // The refill may leave the Home as soon as the buffer owns the complete
    // victim; it does not wait for the backing write completion.
    accept_memory_request_slot(LINE3, first_memory_txn);
    assert(victim_memory_txn != first_memory_txn)
      else $fatal(1, "victim writeback reused a live demand transaction ID");
    subordinate_responses_in.bits = '0;
    subordinate_responses_in.bits.opcode = DBID_RESP;
    subordinate_responses_in.bits.src_id = MEMORY_ID;
    subordinate_responses_in.bits.tgt_id = HOME_ID;
    subordinate_responses_in.bits.txn_id = victim_memory_txn;
    subordinate_responses_in.bits.dbid_or_group_id = MEMORY_DBID;
    if (INVALID_CASE == 4) subordinate_responses_in.bits.resp_err = 2'd2;
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
    subordinate_responses_in.bits.txn_id = victim_memory_txn;
    subordinate_responses_in.bits.dbid_or_group_id = INVALID_CASE == 5 ? MEMORY_DBID + 1 : MEMORY_DBID;
    return_fill_packet(2'd0, 8'h40, 2'd0, first_memory_txn);
    return_fill_packet(2'd1, 8'h41, 2'd0, first_memory_txn);
    return_fill_packet(2'd2, 8'h42, 2'd0, first_memory_txn);
    return_fill_packet(2'd3, 8'h43, 2'd0, first_memory_txn);
    repeat (3) begin
      assert(!port_out.requester.response_data.valid)
        else $fatal(1, "replacement fill escaped before victim completion");
      tick();
    end
    subordinate_responses_in.valid = 1'b1;
    #1;
    assert (port_out.subordinate.rsp.ready)
      else $fatal(1, "inclusive Home did not accept victim completion");
    tick();
    subordinate_responses_in = '0;
    while (!port_out.requester.response_data.valid) tick();
    accept_cached_packet(2'd0, 128'h40);
    accept_cached_packet(2'd1, 128'h41);
    accept_cached_packet(2'd2, 128'h42);
    accept_cached_packet(2'd3, 128'h43);

    // A resident hit updates tree-PLRU state: after filling LINE0 then LINE2,
    // touching LINE0 makes LINE2 the victim for LINE3.
    reset = 1; tick(); reset = 0;
    send_request(LINE0, READ_NO_SNP); tick(); fill_and_return(LINE0, 8'h50);
    send_request(LINE2, READ_NO_SNP); tick(); fill_and_return(LINE2, 8'h70);
    for (int packet = 0; packet < 4; packet++) expected_line[packet] = 128'h50 + 128'(packet);
    send_request(LINE0, READ_NO_SNP); finish_cached();
    send_request(LINE3, READ_NO_SNP);
    while (!port_out.subordinate.req.valid && !port_out.requester.snoops.valid) tick();
    assert (port_out.subordinate.req.valid && !port_out.requester.snoops.valid)
      else $fatal(1, "inclusive Home did not fast-path a zero-snoop clean victim");
    fill_and_return(LINE3, 8'h40);
    send_request(LINE2, READ_NO_SNP); repeat (3) tick(); fill_and_return(LINE2, 8'h70);

    // ReadOnce must preserve an early-beat error and must not cache a failed fill.
    reset = 1; tick(); reset = 0;
    send_request(LINE0, READ_ONCE, 6'd6, HTIF_ID, 0, 1);
    tick();
    accept_memory_request(LINE0, READ_NO_SNP);
    for (int packet = 0; packet < 4; packet++)
      return_fill_packet(2'(packet), 8'(packet), packet == 0 ? 2'b10 : 2'b00);
    for (int packet = 0; packet < 4; packet++)
      accept_cached_packet(2'(packet), 128'(packet), 2'b10);
    send_request(LINE0, READ_ONCE, 6'd6, HTIF_ID, 0, 1);
    tick();
    fill_and_return(LINE0, 8'h50);

    // Snapshot readers do not acquire residency. Repeated polls stay in LLC.
    for (int packet = 0; packet < 4; packet++) expected_line[packet] = 128'h50 + 128'(packet);
    repeat (4) begin send_request(LINE0, READ_ONCE); finish_cached(); end

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
    clean_snoops_back_to_back(INSTRUCTION_ID, DATA_ID, 5'h03, 3'd1);
    finish_cached();

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

    // Final DAT releases the datapath while distinct Home DBIDs retain two
    // delayed acknowledgements. Only another acknowledgement-bearing request
    // is blocked when the small table is full.
    send_request(LINE0, 7'h02, 6'd6, INSTRUCTION_ID, 1);
    finish_cached();
    first_comp_ack_dbid = response_dbid;
    repeat (5) begin
      assert(port_out.requester.requests.ready)
        else $fatal(1, "CompAck retained the inclusive Home datapath");
      tick();
    end
    send_request(LINE0, 7'h03, 6'd6, HTIF_ID, 1);
    clean_snoop(INSTRUCTION_ID, 5'h03, 3'd1);
    finish_cached();
    second_comp_ack_dbid = response_dbid;
    assert(first_comp_ack_dbid != second_comp_ack_dbid)
      else $fatal(1, "inclusive Home reused a live CompAck DBID");

    requester_requests_in.bits = '0;
    requester_requests_in.bits.src_id = HTIF_ID;
    requester_requests_in.bits.tgt_id = HOME_ID;
    requester_requests_in.bits.opcode = READ_ONCE;
    requester_requests_in.bits.address = LINE0;
    requester_requests_in.bits.size_or_num_req = 6'd6;
    requester_requests_in.bits.return_nid_or_stash_nid_or_data_target = HTIF_ID;
    requester_requests_in.bits.return_txn_id_or_stash_lpid = 12'h654;
    requester_requests_in.bits.exp_comp_ack = 1'b1;
    requester_requests_in.valid = 1'b1;
    #1;
    assert(!port_out.requester.requests.ready)
      else $fatal(1, "inclusive Home overcommitted its CompAck table");
    requester_requests_in = '0;

    send_request(LINE0, READ_ONCE);
    while (!port_out.requester.snoops.valid) tick();
    send_comp_ack(HTIF_ID, second_comp_ack_dbid);
    assert(port_out.requester.snoops.valid)
      else $fatal(1, "late CompAck disturbed the active LLC transaction");
    clean_snoop(INSTRUCTION_ID, 5'h03, 3'd1);
    finish_cached();
    assert(port_out.requester.requests.ready)
      else $fatal(1, "late CompAck did not release only its table entry");
    send_comp_ack(INSTRUCTION_ID, first_comp_ack_dbid);
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

    // A dirty copyback updates the resident LLC line and removes the owner.
    send_request(LINE0, 7'h07, 6'd6, DATA_ID); finish_cached();
    copyback(LINE0, 3'b110);
    for (int packet = 0; packet < 4; packet++) expected_line[packet] = 128'hdead0000 + 128'(packet);
    send_request(LINE0, 7'h03); finish_cached();

    // A snoop can have consumed the dirty version before granting copyback.
    // Clean and Invalid returns must not resurrect their older bytes.
    for (int state_index = 0; state_index < 3; state_index++) begin
      copyback(LINE0, state_index == 0 ? 3'd0 : state_index == 1 ? 3'd1 : 3'd2);
      send_request(LINE0, 7'h03); finish_cached();
    end
    // A late Invalid copyback for an absent line neither allocates nor refills.
    copyback(LINE0 + 44'h10000, 3'd0);
    send_request(LINE0, 7'h03); finish_cached();

    // A failed buffered writeback drains its already-issued refill, returns an
    // error, and restores the complete post-snoop dirty victim in its old way.
    reset = 1; tick(); reset = 0;
    send_request(LINE0, READ_NO_SNP); tick(); fill_and_return(LINE0, 8'h80);
    send_request(LINE2, READ_NO_SNP); tick(); fill_and_return(LINE2, 8'h30);
    for (int packet = 0; packet < 4; packet++) expected_line[packet] = 128'h80 + 128'(packet);
    send_request(LINE0, 7'h07, 6'd6, DATA_ID); repeat (3) tick(); finish_cached();
    for (int packet = 0; packet < 4; packet++) expected_line[packet] = 128'h30 + 128'(packet);
    send_request(LINE2, READ_NO_SNP); repeat (3) tick(); finish_cached();
    for (int packet = 0; packet < 4; packet++) expected_line[packet] = 128'h80 + 128'(packet);
    send_request(LINE3, READ_NO_SNP); tick(); dirty_snoop(DATA_ID, 8'hd0);
    for (int packet = 0; packet < 4; packet++)
      for (int byte_index = 0; byte_index < 16; byte_index++)
        if (SNOOP_MASKS[packet * 16 + byte_index])
          expected_line[packet][byte_index * 8 +: 8] = 8'hd0 + 8'(packet);
    accept_memory_request_transaction(LINE0, WRITE_NO_SNP_FULL, victim_memory_txn);
    accept_memory_request_slot(LINE3, first_memory_txn);
    // Separate write responses can arrive in either order. An errored Comp is
    // retained while the later DBIDResp enables the complete data transfer.
    subordinate_responses_in = '0;
    subordinate_responses_in.bits.opcode = COMP;
    subordinate_responses_in.bits.src_id = MEMORY_ID;
    subordinate_responses_in.bits.tgt_id = HOME_ID;
    subordinate_responses_in.bits.txn_id = victim_memory_txn;
    subordinate_responses_in.bits.resp_err = 2'd2;
    subordinate_responses_in.bits.dbid_or_group_id = MEMORY_DBID;
    subordinate_responses_in.valid = 1'b1;
    tick(); subordinate_responses_in = '0;
    subordinate_responses_in.bits.opcode = DBID_RESP;
    subordinate_responses_in.bits.src_id = MEMORY_ID;
    subordinate_responses_in.bits.tgt_id = HOME_ID;
    subordinate_responses_in.bits.txn_id = victim_memory_txn;
    subordinate_responses_in.bits.dbid_or_group_id = MEMORY_DBID;
    subordinate_responses_in.valid = 1'b1;
    tick(); subordinate_responses_in = '0;
    for (int packet = 0; packet < 4; packet++)
      accept_victim_packet(packet[1:0], expected_line[packet]);
    for (int packet = 0; packet < 4; packet++)
      return_fill_packet(packet[1:0], 8'h90 + packet[7:0], 2'd0, first_memory_txn);
    while (!port_out.requester.response_data.valid) tick();
    for (int packet = 0; packet < 4; packet++)
      accept_cached_packet(packet[1:0], 128'h90 + 128'(packet), 2'd2);
    send_request(LINE0, READ_NO_SNP); repeat (3) tick(); finish_cached();

    $display("CHI inclusive Home residency, copyback, response errors, and storage simulation passed");
`ifdef CHI_HOME_TRACE
    event_home_finish();
`endif
    $finish;
  end
endmodule

module chi_copyback_mask_tb;
  chi_inclusive_home_tb #(.INVALID_CASE(1)) test();
endmodule
module chi_copyback_duplicate_tb;
  chi_inclusive_home_tb #(.INVALID_CASE(2)) test();
endmodule
module chi_copyback_state_tb;
  chi_inclusive_home_tb #(.INVALID_CASE(3)) test();
endmodule
module chi_victim_dbid_error_tb;
  chi_inclusive_home_tb #(.INVALID_CASE(4)) test();
endmodule
module chi_victim_comp_dbid_tb;
  chi_inclusive_home_tb #(.INVALID_CASE(5)) test();
endmodule
