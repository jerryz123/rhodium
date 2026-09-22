// Verifies ReadOnce allocation and coherent-observation policy at an inclusive Home.
// SPDX-License-Identifier: Apache-2.0
module chi_read_once_home_tb;
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

  localparam logic [6:0] READ_CLEAN = 7'h02;
  localparam logic [6:0] READ_ONCE = 7'h03;
  localparam logic [6:0] READ_NO_SNP = 7'h04;
  localparam logic [6:0] READ_UNIQUE = 7'h07;
  localparam logic [4:0] SNP_RESP = 5'h01;
  localparam logic [4:0] COMP_ACK = 5'h02;
  localparam logic [4:0] SNP_ONCE = 5'h03;
  localparam logic [3:0] SNP_RESP_DATA_PTL = 4'h5;
  localparam logic [3:0] COMP_DATA = 4'h4;
  localparam logic [6:0] READER_ID = 7'h01;
  localparam logic [6:0] OWNER_ID = 7'h03;
  localparam logic [6:0] HOME_ID = 7'h05;
  localparam logic [6:0] MEMORY_ID = 7'h09;
  localparam logic [43:0] LINE0 = 44'h080000000;
  localparam logic [43:0] LINE2 = 44'h080000200;
  localparam logic [43:0] STREAM0 = 44'h080000400;
  localparam logic [43:0] STREAM1 = 44'h080000600;
  localparam logic [43:0] STREAM2 = 44'h080000800;

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
  logic [11:0] response_dbid;
  logic [3:0] active_qos;

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

  task automatic tick;
    begin
      @(posedge clock);
      #1;
    end
  endtask

  task automatic send_read(input logic [43:0] address,
                           input logic [6:0] opcode,
                           input logic [6:0] source,
                           input bit allocate = 0);
    begin
      requester_requests_in.bits = '0;
      requester_requests_in.bits.src_id = source;
      requester_requests_in.bits.tgt_id = HOME_ID;
      requester_requests_in.bits.opcode = opcode;
      requester_requests_in.bits.address = address;
      requester_requests_in.bits.size_or_num_req = 6'd6;
      requester_requests_in.bits.return_nid_or_stash_nid_or_data_target = source;
      requester_requests_in.bits.return_txn_id_or_stash_lpid = 12'h654;
      requester_requests_in.bits.trace_tag = 1'b1;
      requester_requests_in.bits.exp_comp_ack = 1'b1;
      requester_requests_in.bits.mem_attr.allocate = allocate;
      requester_requests_in.bits.qos = 4'ha;
      active_qos = requester_requests_in.bits.qos;
      requester_requests_in.valid = 1'b1;
      #1;
      assert(port_out.requester.requests.ready)
        else $fatal(1, "inclusive Home did not accept an idle read");
      tick();
      requester_requests_in = '0;
    end
  endtask

  task automatic accept_memory_read(input logic [43:0] address,
                                    input int stall_cycles = 0);
    CHIReqFlit held_request;
    begin
      while (!port_out.subordinate.req.valid) begin
        assert(!port_out.requester.snoops.valid)
          else $fatal(1, "ReadOnce miss snooped an absent line");
        tick();
      end
      #1;
      assert(port_out.subordinate.req.bits.address == address &&
             port_out.subordinate.req.bits.size_or_num_req == 6'd6 &&
             port_out.subordinate.req.bits.opcode == READ_NO_SNP)
        else $fatal(1, "inclusive Home issued an incorrect backing read");
      assert(!port_out.requester.snoops.valid)
        else $fatal(1, "ReadOnce miss snooped an absent line");
      held_request = port_out.subordinate.req.bits;
      repeat (stall_cycles) begin
        tick();
        assert(port_out.subordinate.req.valid && port_out.subordinate.req.bits === held_request)
          else $fatal(1, "stalled backing read changed");
      end
      subordinate_requests_ready_in.ready = 1'b1;
      tick();
      subordinate_requests_ready_in = '0;
    end
  endtask

  task automatic return_fill(input logic [7:0] payload_base,
                             input logic [1:0] first_error = 0);
    begin
      for (int packet = 0; packet < 4; packet++) begin
        subordinate_data_in.bits = '0;
        subordinate_data_in.bits.opcode = COMP_DATA;
        subordinate_data_in.bits.resp_err = packet == 0 ? first_error : 0;
        subordinate_data_in.bits.src_id = MEMORY_ID;
        subordinate_data_in.bits.tgt_id = HOME_ID;
        subordinate_data_in.bits.data_id = packet[1:0];
        subordinate_data_in.bits.byte_enable = 16'hffff;
        subordinate_data_in.bits.data = {120'h0, payload_base + packet[7:0]};
        subordinate_data_in.valid = 1'b1;
        #1;
        assert(port_out.subordinate.dat.response.ready)
          else $fatal(1, "inclusive Home did not accept backing data");
        tick();
        subordinate_data_in = '0;
      end
    end
  endtask

  task automatic accept_line(input logic [7:0] payload_base,
                             input logic [2:0] state,
                             input logic [1:0] error = 0,
                             input int initial_stall = 0,
                             input bit require_quiet = 0);
    CHIDatFlit held_packet;
    begin
      while (!port_out.requester.response_data.valid) begin
        if (require_quiet)
          assert(!port_out.requester.snoops.valid && !port_out.subordinate.req.valid)
            else $fatal(1, "resident line generated an unnecessary snoop or backing read");
        tick();
      end
      held_packet = port_out.requester.response_data.bits;
      if (require_quiet)
        assert(!port_out.requester.snoops.valid && !port_out.subordinate.req.valid)
          else $fatal(1, "resident line generated an unnecessary snoop or backing read");
      repeat (initial_stall) begin
        tick();
        assert(port_out.requester.response_data.valid &&
               port_out.requester.response_data.bits === held_packet)
          else $fatal(1, "stalled Home response data changed");
        if (require_quiet)
          assert(!port_out.requester.snoops.valid && !port_out.subordinate.req.valid)
            else $fatal(1, "resident line generated traffic while its response stalled");
      end
      for (int packet = 0; packet < 4; packet++) begin
        #1;
        if (require_quiet)
          assert(!port_out.requester.snoops.valid && !port_out.subordinate.req.valid)
            else $fatal(1, "resident line generated traffic with response data");
        assert(port_out.requester.response_data.valid &&
               port_out.requester.response_data.bits.opcode == COMP_DATA &&
               port_out.requester.response_data.bits.data_id == packet[1:0] &&
               port_out.requester.response_data.bits.data == {120'h0, payload_base + packet[7:0]} &&
               port_out.requester.response_data.bits.resp_err == error)
          else $fatal(1, "inclusive Home returned incorrect ReadOnce data");
        if (error == 0)
          assert(port_out.requester.response_data.bits.resp == state)
            else $fatal(1, "inclusive Home returned incorrect coherent state");
        if (packet == 0)
          response_dbid = port_out.requester.response_data.bits.dbid_or_mecid[11:0];
        response_data_ready_in.ready = 1'b1;
        tick();
        response_data_ready_in = '0;
      end
    end
  endtask

  task automatic delayed_comp_ack(input logic [6:0] source,
                                  input int delay_cycles = 0);
    begin
      repeat (delay_cycles) begin
        assert(port_out.requester.requests.ready)
          else $fatal(1, "CompAck retained the inclusive Home datapath");
        tick();
      end
      requester_responses_in.bits = '0;
      requester_responses_in.bits.opcode = COMP_ACK;
      requester_responses_in.bits.txn_id = response_dbid;
      requester_responses_in.bits.src_id = source;
      requester_responses_in.bits.tgt_id = HOME_ID;
      requester_responses_in.bits.qos = active_qos;
      requester_responses_in.valid = 1'b1;
      #1;
      assert(port_out.requester.requester_responses.ready)
        else $fatal(1, "inclusive Home did not accept CompAck");
      tick();
      requester_responses_in = '0;
      assert(port_out.requester.requests.ready)
        else $fatal(1, "CompAck disturbed the released inclusive Home datapath");
    end
  endtask

  task automatic clean_snoop(input int stall_cycles = 0);
    CHISnoopDispatch held_snoop;
    begin
      while (!port_out.requester.snoops.valid) tick();
      #1;
      assert(port_out.requester.snoops.bits.target_id == OWNER_ID &&
             port_out.requester.snoops.bits.flit.opcode == SNP_ONCE)
        else $fatal(1, "ReadOnce did not issue SnpOnce to the resident owner");
      held_snoop = port_out.requester.snoops.bits;
      repeat (stall_cycles) begin
        tick();
        assert(port_out.requester.snoops.valid && port_out.requester.snoops.bits === held_snoop)
          else $fatal(1, "stalled SnpOnce changed");
      end
      snoops_ready_in.ready = 1'b1;
      tick();
      snoops_ready_in = '0;
      requester_responses_in.bits = '0;
      requester_responses_in.bits.opcode = SNP_RESP;
      requester_responses_in.bits.src_id = OWNER_ID;
      requester_responses_in.bits.tgt_id = HOME_ID;
      requester_responses_in.bits.txn_id = held_snoop.flit.txn_id;
      requester_responses_in.bits.resp = 3'd1;
      requester_responses_in.valid = 1'b1;
      #1;
      assert(port_out.requester.requester_responses.ready)
        else $fatal(1, "inclusive Home did not accept clean SnpResp");
      tick();
      requester_responses_in = '0;
    end
  endtask

  task automatic dirty_snoop(input logic [7:0] payload_base);
    logic [11:0] snoop_txn_id;
    begin
      while (!port_out.requester.snoops.valid) tick();
      #1;
      assert(port_out.requester.snoops.bits.target_id == OWNER_ID &&
             port_out.requester.snoops.bits.flit.opcode == SNP_ONCE)
        else $fatal(1, "ReadOnce did not snoop the dirty RN-F owner");
      snoop_txn_id = port_out.requester.snoops.bits.flit.txn_id;
      snoops_ready_in.ready = 1'b1;
      tick();
      snoops_ready_in = '0;
      for (int packet = 0; packet < 4; packet++) begin
        request_data_in.bits = '0;
        request_data_in.bits.opcode = SNP_RESP_DATA_PTL;
        request_data_in.bits.src_id = OWNER_ID;
        request_data_in.bits.tgt_id = HOME_ID;
        request_data_in.bits.txn_id = snoop_txn_id;
        request_data_in.bits.data_id = packet[1:0];
        request_data_in.bits.byte_enable = 16'hffff;
        request_data_in.bits.resp = 3'b100;
        request_data_in.bits.data = {120'h0, payload_base + packet[7:0]};
        request_data_in.valid = 1'b1;
        #1;
        assert(port_out.requester.request_data.ready)
          else $fatal(1, "inclusive Home did not accept dirty snoop data");
        tick();
        request_data_in = '0;
      end
    end
  endtask

  task automatic fill_read(input logic [43:0] address,
                           input logic [7:0] payload_base,
                           input bit allocate,
                           input logic [1:0] error = 0,
                           input int request_stall = 0,
                           input int response_stall = 0);
    begin
      send_read(address, READ_ONCE, READER_ID, allocate);
      accept_memory_read(address, request_stall);
      return_fill(payload_base, error);
      accept_line(payload_base, 3'd0, error, response_stall);
      delayed_comp_ack(READER_ID, 2);
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
    repeat (3) tick();
    reset = 1'b0;
    tick();

    // An allocating miss tolerates backing and requester backpressure, fills
    // the LLC, and leaves only its DBID-table entry live until delayed CompAck.
    fill_read(LINE0, 8'h10, 1'b1, 0, 3, 3);

    // Give the RN-F a clean shared copy, then prove SnpOnce retains that copy
    // by observing it again on the following nonallocating snapshot.
    send_read(LINE0, READ_CLEAN, OWNER_ID);
    accept_line(8'h10, 3'd1, 0, 0, 1);
    delayed_comp_ack(OWNER_ID);
    repeat (2) begin
      send_read(LINE0, READ_ONCE, READER_ID);
      clean_snoop(2);
      accept_line(8'h10, 3'd0);
      delayed_comp_ack(READER_ID);
    end

    // Upgrade the RN-F, then let it supply data newer than the LLC. PassDirty
    // plus Invalid installs that data and removes the owner, so the next read
    // is an LLC-only hit with neither a snoop nor a DRAM request.
    send_read(LINE0, READ_UNIQUE, OWNER_ID);
    accept_line(8'h10, 3'd2, 0, 0, 1);
    delayed_comp_ack(OWNER_ID);
    send_read(LINE0, READ_ONCE, READER_ID);
    dirty_snoop(8'h40);
    accept_line(8'h40, 3'd0);
    delayed_comp_ack(READER_ID);
    send_read(LINE0, READ_ONCE, READER_ID);
    accept_line(8'h40, 3'd0, 0, 2, 1);
    delayed_comp_ack(READER_ID);

    // Occupy the other way in the same set. Streaming nonallocating misses,
    // including an errored response, must bypass the LLC and leave both lines.
    fill_read(LINE2, 8'h60, 1'b1);
    fill_read(STREAM0, 8'h80, 1'b0, 2'b10);
    fill_read(STREAM0, 8'h90, 1'b0);
    fill_read(STREAM1, 8'ha0, 1'b0);
    fill_read(STREAM2, 8'hb0, 1'b0);

    send_read(LINE0, READ_ONCE, READER_ID);
    accept_line(8'h40, 3'd0, 0, 0, 1);
    delayed_comp_ack(READER_ID);
    send_read(LINE2, READ_ONCE, READER_ID);
    accept_line(8'h60, 3'd0, 0, 0, 1);
    delayed_comp_ack(READER_ID);

    $display("CHI inclusive Home ReadOnce allocation and coherent observation passed");
    $finish;
  end

  initial begin
    #50000;
    $fatal(1, "ReadOnce Home policy timeout");
  end
endmodule
