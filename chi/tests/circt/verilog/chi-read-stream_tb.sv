// Checks concurrent CHI ReadOnce routing, buffering, restart, and underflow.
// SPDX-License-Identifier: Apache-2.0
module chi_read_stream_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_t;
  typedef struct packed { ready_t requester; rsp_t response; } rsp_in_t;
  typedef struct packed { rsp_t requester; ready_t response; } rsp_out_t;
  typedef struct packed { ready_t request; dat_t response; } dat_in_t;
  typedef struct packed { dat_t request; ready_t response; } dat_out_t;
  typedef struct packed { ready_t req; rsp_in_t rsp; dat_in_t dat; } chi_in_t;
  typedef struct packed { req_t req; rsp_out_t rsp; dat_out_t dat; } chi_out_t;

  localparam logic [6:0] READ_ONCE = 7'h03;
  localparam logic [4:0] COMP_ACK = 5'h02;
  localparam logic [4:0] RETRY_ACK = 5'h07;
  localparam logic [4:0] PCRD_GRANT = 5'h03;
  localparam logic [3:0] COMP_DATA = 4'h4;
  localparam logic [6:0] NODE_ID = 7'h02;
  localparam logic [6:0] HOME_ID = 7'h05;

  logic clock = 1'b0;
  logic reset = 1'b1;
  logic [6:0] node_id = NODE_ID;
  struct packed { logic valid; CHIReadStreamCommand bits; } command_in;
  ready_t command_out;
  ready_t line_in;
  struct packed { logic valid; CHIReadStreamLine bits; } line_out;
  chi_in_t chi_in;
  chi_out_t chi_out;
  logic active;
  logic done;
  logic underflow;
  logic starved;
  int acknowledgements = 0;

  CHIReadStream dut (.*);
  always #5 clock = ~clock;
  always @(posedge clock)
    if (!reset && chi_out.rsp.requester.valid && chi_in.rsp.requester.ready)
      acknowledgements <= acknowledgements + 1;

  function automatic logic [511:0] line_payload(input logic [7:0] base);
    line_payload = {{120'h0, base + 8'd3},
                    {120'h0, base + 8'd2},
                    {120'h0, base + 8'd1},
                    {120'h0, base + 8'd0}};
  endfunction

  task automatic tick;
    begin
      @(posedge clock);
      #1;
    end
  endtask

  task automatic start_stream(input logic [43:0] address,
                              input logic [43:0] byte_count,
                              input logic [43:0] stride);
    begin
      command_in = '0;
      command_in.valid = 1'b1;
      command_in.bits.start_address = address;
      command_in.bits.byte_count = byte_count;
      command_in.bits.stride = stride;
      command_in.bits.home_id = HOME_ID;
      command_in.bits.pas = 3'h3;
      command_in.bits.allocate = 1'b0;
      command_in.bits.qos = 4'ha;
      #1;
      assert(command_out.ready) else $fatal(1, "CHI line stream rejected its command");
      tick();
      command_in = '0;
    end
  endtask

  task automatic accept_request(input logic [43:0] address,
                                input logic [11:0] txn_id,
                                input bit retried = 0);
    begin
      while (!chi_out.req.valid) tick();
      #1;
      assert(chi_out.req.bits.address == address &&
             chi_out.req.bits.txn_id == txn_id &&
             chi_out.req.bits.return_txn_id_or_stash_lpid == txn_id &&
             chi_out.req.bits.src_id == NODE_ID &&
             chi_out.req.bits.tgt_id == HOME_ID &&
             chi_out.req.bits.opcode == READ_ONCE &&
             chi_out.req.bits.size_or_num_req == 6'd6 &&
             chi_out.req.bits.mem_attr.allocate == 1'b0 &&
             chi_out.req.bits.pas == 3'h3 &&
             chi_out.req.bits.qos == 4'ha &&
             chi_out.req.bits.allow_retry == !retried &&
             chi_out.req.bits.pcrd_type == (retried ? 4'hb : 4'h0))
        else $fatal(1, "CHI line stream issued an incorrect ReadOnce request");
      chi_in.req.ready = 1'b1;
      tick();
      chi_in.req = '0;
    end
  endtask

  task automatic send_response(input logic [11:0] txn_id,
                               input logic [4:0] opcode,
                               input logic [3:0] credit_type);
    begin
      chi_in.rsp.response = '0;
      chi_in.rsp.response.valid = 1'b1;
      chi_in.rsp.response.bits.opcode = opcode;
      chi_in.rsp.response.bits.pcrd_type = credit_type;
      chi_in.rsp.response.bits.txn_id = txn_id;
      chi_in.rsp.response.bits.src_id = HOME_ID;
      chi_in.rsp.response.bits.tgt_id = NODE_ID;
      #1;
      assert(chi_out.rsp.response.ready)
        else $fatal(1, "CHI line stream did not route a response");
      tick();
      chi_in.rsp.response = '0;
    end
  endtask

  task automatic send_line(input logic [11:0] txn_id,
                           input logic [11:0] dbid,
                           input logic [7:0] base,
                           input logic [1:0] error = 0,
                           input bit poisoned = 0);
    begin
      for (int packet = 0; packet < 4; packet++) begin
        chi_in.dat.response = '0;
        chi_in.dat.response.valid = 1'b1;
        chi_in.dat.response.bits.opcode = COMP_DATA;
        chi_in.dat.response.bits.resp_err = packet == 0 ? error : 0;
        chi_in.dat.response.bits.poison = packet == 0 && poisoned ? 2'b01 : 0;
        chi_in.dat.response.bits.src_id = HOME_ID;
        chi_in.dat.response.bits.tgt_id = NODE_ID;
        chi_in.dat.response.bits.home_nid_or_pbha_or_mismatched_mecid = HOME_ID;
        chi_in.dat.response.bits.txn_id = txn_id;
        chi_in.dat.response.bits.dbid_or_mecid = {4'h0, dbid};
        chi_in.dat.response.bits.data_id = packet[1:0];
        chi_in.dat.response.bits.byte_enable = 16'hffff;
        chi_in.dat.response.bits.data = {120'h0, base + packet[7:0]};
        #1;
        assert(chi_out.dat.response.ready)
          else $fatal(1, "CHI line stream did not route response data");
        tick();
        chi_in.dat.response = '0;
      end
    end
  endtask

  task automatic accept_line(input logic [43:0] address,
                             input logic [7:0] base,
                             input logic [6:0] valid_bytes,
                             input bit last,
                             input logic [1:0] error = 0,
                             input bit poisoned = 0);
    begin
      while (!line_out.valid) tick();
      #1;
      assert(line_out.bits.address == address &&
             line_out.bits.data == line_payload(base) &&
             line_out.bits.valid_bytes == valid_bytes &&
             line_out.bits.last == last &&
             line_out.bits.error == error &&
             line_out.bits.poisoned == poisoned)
        else $fatal(1, "CHI line stream returned an incorrect line");
      line_in.ready = 1'b1;
      #1;
      assert(done == last) else $fatal(1, "CHI line stream final marker mismatch");
      tick();
      line_in = '0;
    end
  endtask

  initial begin
    command_in = '0;
    line_in = '0;
    chi_in = '0;
    chi_in.rsp.requester.ready = 1'b1;
    repeat (3) tick();
    reset = 1'b0;
    tick();

    start_stream(44'h1000, 44'd160, 44'h40);

    accept_request(44'h1000, 12'd16);
    accept_request(44'h1040, 12'd17);
    accept_request(44'h1080, 12'd18);

    // Route retry traffic to the middle engine without disturbing the other
    // transactions or the stream order.
    send_response(12'd17, RETRY_ACK, 4'hb);
    send_response(12'd17, PCRD_GRANT, 4'hb);
    accept_request(44'h1040, 12'd17, 1'b1);

    line_in.ready = 1'b1;
    tick();
    assert(underflow && starved) else $fatal(1, "CHI line stream did not expose starvation");
    line_in = '0;

    send_line(12'd18, 12'h118, 8'h30);
    send_line(12'd16, 12'h116, 8'h10);
    send_line(12'd17, 12'h117, 8'h20, 2'b10, 1'b1);
    while (acknowledgements != 3) tick();
    accept_line(44'h1000, 8'h10, 7'd64, 1'b0);
    accept_line(44'h1040, 8'h20, 7'd64, 1'b0, 2'b10, 1'b1);
    accept_line(44'h1080, 8'h30, 7'd32, 1'b1);
    assert(!active && starved) else $fatal(1, "CHI line stream did not retire correctly");
    assert(!chi_out.dat.request.valid) else $fatal(1, "read-only stream emitted request data");

    // A restart discards an already buffered line, retains the TxnID of an
    // older in-flight line, and uses the next free reader for the new stream.
    start_stream(44'h2000, 44'd128, 44'h40);
    accept_request(44'h2000, 12'd16);
    accept_request(44'h2040, 12'd17);
    send_line(12'd16, 12'h216, 8'h40);
    while (acknowledgements != 4) tick();
    while (!line_out.valid) tick();
    start_stream(44'h4000, 44'd64, 44'h40);
    accept_request(44'h4000, 12'd18);
    send_line(12'd18, 12'h418, 8'h50);
    while (acknowledgements != 5) tick();
    accept_line(44'h4000, 8'h50, 7'd64, 1'b1);
    send_line(12'd17, 12'h217, 8'h41);
    while (acknowledgements != 6) tick();

    // All three TxnIDs become reusable after the late old completion.
    start_stream(44'h5000, 44'd192, 44'h40);
    accept_request(44'h5000, 12'd16);
    accept_request(44'h5040, 12'd17);
    accept_request(44'h5080, 12'd18);
    send_line(12'd16, 12'h516, 8'h60);
    send_line(12'd17, 12'h517, 8'h61);
    send_line(12'd18, 12'h518, 8'h62);
    while (acknowledgements != 9) tick();
    accept_line(44'h5000, 8'h60, 7'd64, 1'b0);
    accept_line(44'h5040, 8'h61, 7'd64, 1'b0);
    accept_line(44'h5080, 8'h62, 7'd64, 1'b1);

    $display("CHI line stream concurrency, retry routing, restart, and delivery passed");
    $finish;
  end

  initial begin
    #30000;
    $fatal(1, "CHI line stream timeout");
  end
endmodule
