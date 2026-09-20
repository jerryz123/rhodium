// Simulates both sides of a two-subordinate CHI SN NoC attachment.
// SPDX-License-Identifier: Apache-2.0
module chi_sn_noc_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_t;
  typedef struct packed {
    ready_t rsp;
    req_t req;
    struct packed {
      dat_t request;
      ready_t response;
    } dat;
  } home_in_t;
  typedef struct packed {
    rsp_t rsp;
    ready_t req;
    struct packed {
      ready_t request;
      dat_t response;
    } dat;
  } home_out_t;
  typedef struct packed {
    rsp_t rsp;
    ready_t req;
    struct packed {
      ready_t request;
      dat_t response;
    } dat;
  } node_in_t;
  typedef struct packed {
    ready_t rsp;
    req_t req;
    struct packed {
      dat_t request;
      ready_t response;
    } dat;
  } node_out_t;

  localparam logic [6:0] HOME_ID = 7'd5;
  localparam logic [6:0] FIRST_ID = 7'd9;
  localparam logic [6:0] SECOND_ID = 7'd10;

  logic clock = 1'b0;
  logic reset = 1'b1;
  home_in_t home_in;
  node_in_t first_in;
  node_in_t second_in;
  home_out_t home_out;
  node_out_t first_out;
  node_out_t second_out;

  CHISNNoCFixture dut (.*);
  always #5 clock = ~clock;

  task automatic tick;
    begin
      @(posedge clock);
      #1;
    end
  endtask

  task automatic send_request(
    input logic target_lane,
    input logic [11:0] txn_id
  );
    integer cycles;
    begin
      // Keep the unselected SN ready while the selected SN is blocked. The
      // selected path must hold without leaking into or depending on the other path.
      first_in.req.ready = target_lane;
      second_in.req.ready = !target_lane;
      home_in.req = '0;
      home_in.req.bits.opcode = 7'h04;
      home_in.req.bits.src_id = HOME_ID;
      home_in.req.bits.tgt_id = target_lane ? SECOND_ID : FIRST_ID;
      home_in.req.bits.txn_id = txn_id;
      home_in.req.bits.address = target_lane ? 44'h090000000 : 44'h080000000;
      home_in.req.valid = 1'b1;
      #1;
      while (!home_out.req.ready)
        tick();
      tick();
      home_in.req = '0;

      for (cycles = 0; cycles < 8; cycles = cycles + 1) begin
        #1;
        if (target_lane ? second_out.req.valid : first_out.req.valid)
          break;
        tick();
      end
      assert (cycles < 8)
        else $fatal(1, "REQ did not reach the selected SN");
      assert (!(target_lane ? first_out.req.valid : second_out.req.valid))
        else $fatal(1, "REQ appeared at an unselected SN");
      assert ((target_lane ? second_out.req.bits.tgt_id : first_out.req.bits.tgt_id) ==
                (target_lane ? SECOND_ID : FIRST_ID) &&
              (target_lane ? second_out.req.bits.txn_id : first_out.req.bits.txn_id) == txn_id)
        else $fatal(1, "REQ payload changed in SN transport");
      tick();
      assert (target_lane ? second_out.req.valid : first_out.req.valid)
        else $fatal(1, "REQ did not remain valid under backpressure");
      if (target_lane)
        second_in.req.ready = 1'b1;
      else
        first_in.req.ready = 1'b1;
      tick();
      first_in.req.ready = 1'b0;
      second_in.req.ready = 1'b0;
    end
  endtask

  task automatic send_response(
    input logic source_lane,
    input logic [11:0] txn_id
  );
    integer cycles;
    begin
      home_in.rsp.ready = 1'b0;
      if (source_lane) begin
        second_in.rsp = '0;
        second_in.rsp.bits.opcode = 5'h04;
        second_in.rsp.bits.src_id = SECOND_ID;
        second_in.rsp.bits.tgt_id = HOME_ID;
        second_in.rsp.bits.txn_id = txn_id;
        second_in.rsp.valid = 1'b1;
        #1;
        for (cycles = 0; cycles < 8 && !second_out.rsp.ready; cycles = cycles + 1)
          tick();
        assert (second_out.rsp.ready)
          else $fatal(1, "second SN RSP ingress remained backpressured");
        tick();
        second_in.rsp = '0;
      end else begin
        first_in.rsp = '0;
        first_in.rsp.bits.opcode = 5'h04;
        first_in.rsp.bits.src_id = FIRST_ID;
        first_in.rsp.bits.tgt_id = HOME_ID;
        first_in.rsp.bits.txn_id = txn_id;
        first_in.rsp.valid = 1'b1;
        #1;
        for (cycles = 0; cycles < 8 && !first_out.rsp.ready; cycles = cycles + 1)
          tick();
        assert (first_out.rsp.ready)
          else $fatal(1, "first SN RSP ingress remained backpressured");
        tick();
        first_in.rsp = '0;
      end
      for (cycles = 0; cycles < 8 && !home_out.rsp.valid; cycles = cycles + 1)
        tick();
      assert (home_out.rsp.valid &&
              home_out.rsp.bits.src_id == (source_lane ? SECOND_ID : FIRST_ID) &&
              home_out.rsp.bits.txn_id == txn_id)
        else $fatal(1, "RSP did not return from the selected SN");
      tick();
      assert (home_out.rsp.valid)
        else $fatal(1, "RSP did not remain valid under HN backpressure");
      home_in.rsp.ready = 1'b1;
      tick();
      home_in.rsp.ready = 1'b0;
    end
  endtask

  task automatic send_request_data(
    input logic target_lane,
    input logic [11:0] txn_id,
    input logic [127:0] payload
  );
    integer cycles;
    begin
      if (target_lane)
        second_in.dat.request.ready = 1'b1;
      else
        first_in.dat.request.ready = 1'b1;
      home_in.dat.request = '0;
      home_in.dat.request.bits.opcode = 4'h03;
      home_in.dat.request.bits.src_id = HOME_ID;
      home_in.dat.request.bits.tgt_id = target_lane ? SECOND_ID : FIRST_ID;
      home_in.dat.request.bits.txn_id = txn_id;
      home_in.dat.request.bits.data = payload;
      home_in.dat.request.valid = 1'b1;
      #1;
      while (!home_out.dat.request.ready)
        tick();
      tick();
      home_in.dat.request = '0;

      for (cycles = 0; cycles < 8; cycles = cycles + 1) begin
        #1;
        if (target_lane ? second_out.dat.request.valid : first_out.dat.request.valid)
          break;
        tick();
      end
      assert (cycles < 8)
        else $fatal(1, "request DAT did not reach the selected SN");
      assert (!(target_lane ? first_out.dat.request.valid : second_out.dat.request.valid))
        else $fatal(1, "request DAT appeared at an unselected SN");
      assert ((target_lane ? second_out.dat.request.bits.data : first_out.dat.request.bits.data) == payload)
        else $fatal(1, "request DAT payload changed in SN transport");
      tick();
      first_in.dat.request.ready = 1'b0;
      second_in.dat.request.ready = 1'b0;
    end
  endtask

  task automatic send_response_data(
    input logic source_lane,
    input logic [11:0] txn_id,
    input logic [127:0] payload
  );
    integer cycles;
    begin
      home_in.dat.response.ready = 1'b1;
      if (source_lane) begin
        second_in.dat.response = '0;
        second_in.dat.response.bits.opcode = 4'h04;
        second_in.dat.response.bits.src_id = SECOND_ID;
        second_in.dat.response.bits.tgt_id = HOME_ID;
        second_in.dat.response.bits.txn_id = txn_id;
        second_in.dat.response.bits.data = payload;
        second_in.dat.response.valid = 1'b1;
        #1;
        while (!second_out.dat.response.ready)
          tick();
        tick();
        second_in.dat.response = '0;
      end else begin
        first_in.dat.response = '0;
        first_in.dat.response.bits.opcode = 4'h04;
        first_in.dat.response.bits.src_id = FIRST_ID;
        first_in.dat.response.bits.tgt_id = HOME_ID;
        first_in.dat.response.bits.txn_id = txn_id;
        first_in.dat.response.bits.data = payload;
        first_in.dat.response.valid = 1'b1;
        #1;
        while (!first_out.dat.response.ready)
          tick();
        tick();
        first_in.dat.response = '0;
      end

      for (cycles = 0; cycles < 8; cycles = cycles + 1) begin
        #1;
        if (home_out.dat.response.valid)
          break;
        tick();
      end
      assert (cycles < 8)
        else $fatal(1, "response DAT did not return to the HN");
      assert (home_out.dat.response.bits.src_id == (source_lane ? SECOND_ID : FIRST_ID) &&
              home_out.dat.response.bits.tgt_id == HOME_ID &&
              home_out.dat.response.bits.txn_id == txn_id &&
              home_out.dat.response.bits.data == payload)
        else $fatal(1, "response DAT payload changed in SN transport");
      tick();
      home_in.dat.response.ready = 1'b0;
    end
  endtask

  initial begin
    home_in = '0;
    first_in = '0;
    second_in = '0;
    tick();
    reset = 1'b0;

    send_request(1'b0, 12'h101);
    send_request(1'b1, 12'h102);
    send_response(1'b0, 12'h201);
    send_response(1'b1, 12'h202);
    send_request_data(1'b0, 12'h301, 128'h00112233445566778899aabbccddeeff);
    send_request_data(1'b1, 12'h302, 128'hffeeddccbbaa99887766554433221100);
    send_response_data(1'b0, 12'h401, 128'h0123456789abcdef0123456789abcdef);
    send_response_data(1'b1, 12'h402, 128'hfedcba9876543210fedcba9876543210);

    $display("CHI SN NoC adapter simulation passed");
    $finish;
  end
endmodule
