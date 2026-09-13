// Simulates CHIRam masks, concurrent allocation/DAT, reset recovery, stalls, and address-space-end access.
// SPDX-License-Identifier: Apache-2.0
module chi_ram_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_forward_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_forward_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_forward_t;
  typedef struct packed {
    struct packed { ready_t response; } rsp;
    req_forward_t req;
    struct packed {
      dat_forward_t request;
      ready_t response;
    } dat;
  } sn_in_t;
  typedef struct packed {
    struct packed { rsp_forward_t response; } rsp;
    ready_t req;
    struct packed {
      ready_t request;
      dat_forward_t response;
    } dat;
  } sn_out_t;

  localparam logic [6:0] READ_NO_SNP = 7'h04;
  localparam logic [6:0] WRITE_NO_SNP_PTL = 7'h1c;
  localparam logic [6:0] WRITE_NO_SNP_FULL = 7'h1d;
  localparam logic [4:0] COMP = 5'h04;
  localparam logic [4:0] DBID_RESP = 5'h06;
  localparam logic [3:0] NON_COPY_BACK_WRITE_DATA = 4'h3;
  localparam logic [3:0] COMP_DATA = 4'h4;
  localparam logic [6:0] REQUESTER_ID = 7'h03;
  localparam logic [6:0] RAM_ID = 7'h09;

  logic clock = 1'b0;
  logic reset = 1'b1;
  CHIRamIdentity identity;
  req_forward_t requests_in;
  ready_t requests_out;
  dat_forward_t request_data_in;
  ready_t request_data_out;
  ready_t responses_in;
  rsp_forward_t responses_out;
  ready_t response_data_in;
  dat_forward_t response_data_out;
  sn_in_t port_in;
  sn_out_t port_out;

  assign port_in.rsp.response = responses_in;
  assign port_in.req = requests_in;
  assign port_in.dat.request = request_data_in;
  assign port_in.dat.response = response_data_in;
  assign responses_out = port_out.rsp.response;
  assign requests_out = port_out.req;
  assign request_data_out = port_out.dat.request;
  assign response_data_out = port_out.dat.response;

  CHIRam dut (.*);
  always #5 clock = ~clock;

  task automatic tick;
    begin
      @(posedge clock);
      #1;
    end
  endtask

  task automatic issue_request(
    input logic [6:0] opcode,
    input logic [11:0] txn_id,
    input logic [43:0] address,
    input logic [5:0] size,
    input logic [11:0] return_txn_id
  );
    begin
      requests_in.bits = '0;
      requests_in.bits.opcode = opcode;
      requests_in.bits.src_id = REQUESTER_ID;
      requests_in.bits.tgt_id = RAM_ID;
      requests_in.bits.txn_id = txn_id;
      requests_in.bits.address = address;
      requests_in.bits.size_or_num_req = size;
      requests_in.bits.return_nid_or_stash_nid_or_data_target = REQUESTER_ID;
      requests_in.bits.return_txn_id_or_stash_lpid = return_txn_id;
      requests_in.valid = 1'b1;
      while (!requests_out.ready)
        tick();
      tick();
      requests_in.valid = 1'b0;
      requests_in.bits = '0;
    end
  endtask

  task automatic issue_write_data(
    input logic [11:0] dbid,
    input logic [1:0] data_id,
    input logic [15:0] byte_enable,
    input logic [127:0] data
  );
    begin
      request_data_in.bits = '0;
      request_data_in.bits.opcode = NON_COPY_BACK_WRITE_DATA;
      request_data_in.bits.src_id = REQUESTER_ID;
      request_data_in.bits.tgt_id = RAM_ID;
      request_data_in.bits.txn_id = dbid;
      request_data_in.bits.data_id = data_id;
      request_data_in.bits.byte_enable = byte_enable;
      request_data_in.bits.data = data;
      request_data_in.valid = 1'b1;
      while (!request_data_out.ready)
        tick();
      tick();
      request_data_in.valid = 1'b0;
      request_data_in.bits = '0;
    end
  endtask

  task automatic wait_rsp;
    integer cycles;
    begin
      cycles = 0;
      while (!responses_out.valid && cycles < 100) begin
        tick();
        cycles = cycles + 1;
      end
      assert (responses_out.valid)
        else $fatal(1, "timed out waiting for CHIRam RSP");
    end
  endtask

  task automatic wait_dat;
    integer cycles;
    begin
      cycles = 0;
      while (!response_data_out.valid && cycles < 100) begin
        tick();
        cycles = cycles + 1;
      end
      assert (response_data_out.valid)
        else $fatal(1, "timed out waiting for CHIRam DAT");
    end
  endtask

  task automatic accept_dbid(
    input logic [11:0] request_txn_id,
    output logic [11:0] dbid
  );
    begin
      responses_in.ready = 1'b1;
      wait_rsp();
      assert (responses_out.bits.opcode == DBID_RESP)
        else $fatal(1, "write did not receive DBIDResp");
      assert (responses_out.bits.src_id == RAM_ID &&
              responses_out.bits.tgt_id == REQUESTER_ID &&
              responses_out.bits.txn_id == request_txn_id)
        else $fatal(1, "DBIDResp carried incorrect routing metadata");
      dbid = responses_out.bits.dbid_or_group_id;
      tick();
      responses_in.ready = 1'b0;
    end
  endtask

  task automatic accept_comp(
    input logic [11:0] request_txn_id,
    input logic [11:0] dbid
  );
    begin
      responses_in.ready = 1'b1;
      wait_rsp();
      assert (responses_out.bits.opcode == COMP)
        else $fatal(1, "write did not receive Comp");
      assert (responses_out.bits.src_id == RAM_ID &&
              responses_out.bits.tgt_id == REQUESTER_ID &&
              responses_out.bits.txn_id == request_txn_id &&
              responses_out.bits.dbid_or_group_id == dbid)
        else $fatal(1, "Comp carried incorrect transaction metadata");
      tick();
      responses_in.ready = 1'b0;
    end
  endtask

  task automatic accept_read(
    input logic [11:0] return_txn_id,
    input logic [1:0] data_id,
    input logic [15:0] byte_enable,
    input logic [127:0] data
  );
    begin
      response_data_in.ready = 1'b1;
      wait_dat();
      assert (response_data_out.bits.opcode == COMP_DATA)
        else $fatal(1, "read did not receive CompData");
      assert (response_data_out.bits.src_id == RAM_ID &&
              response_data_out.bits.tgt_id == REQUESTER_ID &&
              response_data_out.bits.txn_id == return_txn_id)
        else $fatal(1, "CompData carried incorrect routing metadata");
      assert (response_data_out.bits.byte_enable == byte_enable)
        else $fatal(1, "CompData carried incorrect byte enable");
      assert (response_data_out.bits.data_id == data_id)
        else $fatal(1, "CompData carried incorrect DataID");
      assert (response_data_out.bits.data == data)
        else $fatal(1, "CompData %h, expected %h", response_data_out.bits.data, data);
      tick();
      response_data_in.ready = 1'b0;
    end
  endtask

  logic [11:0] dbid_a;
  logic [11:0] dbid_b;
  bit saw_concurrent_allocation_data = 0;
  always @(posedge clock)
    if (!reset && requests_in.valid && requests_out.ready &&
        request_data_in.valid && request_data_out.ready)
      saw_concurrent_allocation_data <= 1;

  initial begin
    identity = '{node_id: RAM_ID, base_address: 44'h080000000};
    requests_in = '0;
    request_data_in = '0;
    responses_in = '0;
    response_data_in = '0;
    tick();
    assert (!responses_out.valid && !response_data_out.valid)
      else $fatal(1, "reset did not clear the CHIRam response paths");
    reset = 1'b0;

    issue_request(READ_NO_SNP, 12'h101, 44'h080000000, 6'd4, 12'h501);
    accept_read(12'h501, 2'd0, 16'hffff, 128'b0);

    issue_request(WRITE_NO_SNP_FULL, 12'h102, 44'h080000000, 6'd4, 12'b0);
    accept_dbid(12'h102, dbid_a);
    issue_write_data(dbid_a, 2'd0, 16'hffff, 128'h00112233445566778899aabbccddeeff);
    accept_comp(12'h102, dbid_a);

    issue_request(WRITE_NO_SNP_PTL, 12'h103, 44'h080000000, 6'd4, 12'b0);
    accept_dbid(12'h103, dbid_a);
    issue_write_data(dbid_a, 2'd0, 16'h00ff, 128'hffeeddccbbaa99887766554433221100);
    accept_comp(12'h103, dbid_a);

    issue_request(READ_NO_SNP, 12'h104, 44'h080000000, 6'd4, 12'h504);
    accept_read(12'h504, 2'd0, 16'hffff, 128'h00112233445566777766554433221100);

    issue_request(WRITE_NO_SNP_FULL, 12'h105, 44'h080000000, 6'd6, 12'b0);
    accept_dbid(12'h105, dbid_a);
    issue_write_data(dbid_a, 2'd2, 16'hffff, 128'h22222222222222222222222222222222);
    issue_write_data(dbid_a, 2'd0, 16'hffff, 128'h00000000000000000000000000000000);
    issue_write_data(dbid_a, 2'd3, 16'hffff, 128'h33333333333333333333333333333333);
    issue_write_data(dbid_a, 2'd1, 16'hffff, 128'h11111111111111111111111111111111);
    accept_comp(12'h105, dbid_a);

    issue_request(READ_NO_SNP, 12'h106, 44'h080000000, 6'd6, 12'h506);
    accept_read(12'h506, 2'd0, 16'hffff, 128'h00000000000000000000000000000000);
    accept_read(12'h506, 2'd1, 16'hffff, 128'h11111111111111111111111111111111);
    accept_read(12'h506, 2'd2, 16'hffff, 128'h22222222222222222222222222222222);
    accept_read(12'h506, 2'd3, 16'hffff, 128'h33333333333333333333333333333333);

    // Hold RSP back while both transaction slots allocate distinct live DBIDs.
    issue_request(WRITE_NO_SNP_FULL, 12'h201, 44'h080000010, 6'd4, 12'b0);
    issue_request(WRITE_NO_SNP_FULL, 12'h202, 44'h080000020, 6'd4, 12'b0);
    accept_dbid(12'h201, dbid_a);
    accept_dbid(12'h202, dbid_b);
    assert (dbid_a != dbid_b)
      else $fatal(1, "concurrent CHIRam writes reused a live DBID");

    issue_write_data(dbid_a, 2'd1, 16'hffff, 128'h11111111222222223333333344444444);
    issue_write_data(dbid_b, 2'd2, 16'hffff, 128'haaaaaaaabbbbbbbbccccccccdddddddd);
    accept_comp(12'h201, dbid_a);
    accept_comp(12'h202, dbid_b);

    // DataID is a line position, not an offset to add to these narrow requests.
    issue_request(READ_NO_SNP, 12'h203, 44'h080000010, 6'd4, 12'h503);
    accept_read(12'h503, 2'd1, 16'hffff, 128'h11111111222222223333333344444444);
    issue_request(READ_NO_SNP, 12'h204, 44'h080000020, 6'd5, 12'h504);
    accept_read(12'h504, 2'd2, 16'hffff, 128'haaaaaaaabbbbbbbbccccccccdddddddd);
    accept_read(12'h504, 2'd3, 16'hffff, 128'h33333333333333333333333333333333);

    // Reset one receipt mask while advancing a different live multibeat write.
    issue_request(WRITE_NO_SNP_FULL, 12'h301, 44'h080000000, 6'd5, 12'b0);
    accept_dbid(12'h301, dbid_a);
    fork
      issue_request(WRITE_NO_SNP_FULL, 12'h302, 44'h080000020, 6'd4, 12'b0);
      issue_write_data(dbid_a, 2'd1, 16'hffff, 128'h55555555555555555555555555555555);
    join
    assert (saw_concurrent_allocation_data)
      else $fatal(1, "test did not accept allocation and DAT on the same cycle");
    accept_dbid(12'h302, dbid_b);
    assert (dbid_a != dbid_b) else $fatal(1, "allocation collided with accepted DAT");
    issue_write_data(dbid_a, 2'd0, 16'hffff, 128'h44444444444444444444444444444444);
    accept_comp(12'h301, dbid_a);
    issue_write_data(dbid_b, 2'd2, 16'hffff, 128'h66666666666666666666666666666666);
    accept_comp(12'h302, dbid_b);
    issue_request(READ_NO_SNP, 12'h303, 44'h080000000, 6'd5, 12'h603);
    accept_read(12'h603, 2'd0, 16'hffff, 128'h44444444444444444444444444444444);
    accept_read(12'h603, 2'd1, 16'hffff, 128'h55555555555555555555555555555555);
    issue_request(READ_NO_SNP, 12'h304, 44'h080000020, 6'd4, 12'h604);
    accept_read(12'h604, 2'd2, 16'hffff, 128'h66666666666666666666666666666666);

    // Abandon a partial transaction on reset, then reuse its slot and DataID.
    issue_request(WRITE_NO_SNP_FULL, 12'h305, 44'h080000000, 6'd5, 12'b0);
    accept_dbid(12'h305, dbid_a);
    issue_write_data(dbid_a, 2'd1, 16'hffff, 128'h7777);
    reset = 1'b1;
    tick();
    reset = 1'b0;
    tick();
    assert (!responses_out.valid && !response_data_out.valid)
      else $fatal(1, "reset retained an abandoned transaction response");
    issue_request(WRITE_NO_SNP_FULL, 12'h306, 44'h080000010, 6'd4, 12'b0);
    accept_dbid(12'h306, dbid_b);
    issue_write_data(dbid_b, 2'd1, 16'hffff, 128'h8888);
    accept_comp(12'h306, dbid_b);
    issue_request(READ_NO_SNP, 12'h307, 44'h080000010, 6'd4, 12'h607);
    accept_read(12'h607, 2'd1, 16'hffff, 128'h8888);

    // Move the idle 64-byte window to the top of the physical address space.
    identity.base_address = 44'hfffffffffc0;
    issue_request(READ_NO_SNP, 12'h205, 44'hffffffffff0, 6'd4, 12'h505);
    accept_read(12'h505, 2'd3, 16'hffff, 128'h33333333333333333333333333333333);

    $display("CHI decoupled RAM simulation passed");
    $finish;
  end
endmodule
