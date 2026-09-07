// Simulates fragmented 64-byte CHI RAM writes and reads.
module chi_transfer_fragmenter_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_t;
  typedef struct packed {
    struct packed { ready_t response; } rsp;
    req_t req;
    struct packed { dat_t request; ready_t response; } dat;
  } sn_in_t;
  typedef struct packed {
    struct packed { rsp_t response; } rsp;
    ready_t req;
    struct packed { ready_t request; dat_t response; } dat;
  } sn_out_t;

  localparam logic [6:0] READ_NO_SNP = 7'h04;
  localparam logic [6:0] WRITE_NO_SNP_FULL = 7'h1d;
  localparam logic [4:0] COMP = 5'h04;
  localparam logic [4:0] DBID_RESP = 5'h06;
  localparam logic [3:0] NON_COPY_BACK_WRITE_DATA = 4'h3;
  localparam logic [3:0] COMP_DATA = 4'h4;
  localparam logic [6:0] HOME_ID = 7'h05;
  localparam logic [6:0] RAM_ID = 7'h09;

  logic clock = 1'b0;
  logic reset = 1'b1;
  req_t requests_in;
  ready_t responses_in;
  dat_t request_data_in;
  ready_t response_data_in;
  sn_in_t port_in;
  sn_out_t port_out;

  assign port_in.rsp.response = responses_in;
  assign port_in.req = requests_in;
  assign port_in.dat.request = request_data_in;
  assign port_in.dat.response = response_data_in;

  CHITransferFragmenterFixture dut (.*);
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
      requests_in.bits.src_id = HOME_ID;
      requests_in.bits.tgt_id = RAM_ID;
      requests_in.bits.txn_id = txn_id;
      requests_in.bits.address = address;
      requests_in.bits.size_or_num_req = size;
      requests_in.bits.return_nid_or_stash_nid_or_data_target = HOME_ID;
      requests_in.bits.return_txn_id_or_stash_lpid = return_txn_id;
      requests_in.valid = 1'b1;
      while (!port_out.req.ready)
        tick();
      tick();
      requests_in = '0;
    end
  endtask

  task automatic wait_rsp;
    integer cycles;
    begin
      for (cycles = 0; cycles < 100 && !port_out.rsp.response.valid; cycles = cycles + 1)
        tick();
      assert (port_out.rsp.response.valid)
        else $fatal(1, "timed out waiting for fragmented-path RSP");
    end
  endtask

  task automatic write_word(
    input logic [43:0] address,
    input logic [11:0] txn_id,
    input logic [127:0] data
  );
    logic [11:0] dbid;
    begin
      issue_request(WRITE_NO_SNP_FULL, txn_id, address, 6'd4, 12'b0);
      responses_in.ready = 1'b1;
      wait_rsp();
      assert (port_out.rsp.response.bits.opcode == DBID_RESP)
        else $fatal(1, "single-beat write did not pass through its DBID");
      dbid = port_out.rsp.response.bits.dbid_or_group_id;
      tick();
      responses_in.ready = 1'b0;

      request_data_in.bits = '0;
      request_data_in.bits.opcode = NON_COPY_BACK_WRITE_DATA;
      request_data_in.bits.src_id = HOME_ID;
      request_data_in.bits.tgt_id = RAM_ID;
      request_data_in.bits.txn_id = dbid;
      request_data_in.bits.data_id = address[5:4];
      request_data_in.bits.byte_enable = 16'hffff;
      request_data_in.bits.data = data;
      request_data_in.valid = 1'b1;
      while (!port_out.dat.request.ready)
        tick();
      tick();
      request_data_in = '0;

      responses_in.ready = 1'b1;
      wait_rsp();
      assert (port_out.rsp.response.bits.opcode == COMP &&
              port_out.rsp.response.bits.txn_id == txn_id)
        else $fatal(1, "single-beat write did not pass through completion");
      tick();
      responses_in.ready = 1'b0;
    end
  endtask

  task automatic send_write_beat(
    input logic [11:0] dbid,
    input logic [1:0] data_id,
    input logic [127:0] data
  );
    begin
      request_data_in.bits = '0;
      request_data_in.bits.opcode = NON_COPY_BACK_WRITE_DATA;
      request_data_in.bits.src_id = HOME_ID;
      request_data_in.bits.tgt_id = RAM_ID;
      request_data_in.bits.txn_id = dbid;
      request_data_in.bits.data_id = data_id;
      request_data_in.bits.byte_enable = 16'hffff;
      request_data_in.bits.data = data;
      request_data_in.valid = 1'b1;
      while (!port_out.dat.request.ready)
        tick();
      tick();
      request_data_in = '0;
    end
  endtask

  task automatic write_line;
    logic [11:0] dbid;
    begin
      issue_request(WRITE_NO_SNP_FULL, 12'h100, 44'h080000000, 6'd6, 12'b0);
      responses_in.ready = 1'b1;
      wait_rsp();
      assert (port_out.rsp.response.bits.opcode == DBID_RESP)
        else $fatal(1, "fragmented write did not receive parent DBID");
      dbid = port_out.rsp.response.bits.dbid_or_group_id;
      tick();
      responses_in.ready = 1'b0;

      send_write_beat(dbid, 2'd2, 128'h22222222222222222222222222222222);
      send_write_beat(dbid, 2'd0, 128'h00000000000000000000000000000000);
      send_write_beat(dbid, 2'd3, 128'h33333333333333333333333333333333);
      send_write_beat(dbid, 2'd1, 128'h11111111111111111111111111111111);

      responses_in.ready = 1'b1;
      wait_rsp();
      assert (port_out.rsp.response.bits.opcode == COMP &&
              port_out.rsp.response.bits.txn_id == 12'h100 &&
              port_out.rsp.response.bits.dbid_or_group_id == dbid)
        else $fatal(1, "fragmented write completion metadata was incorrect");
      tick();
      responses_in.ready = 1'b0;
    end
  endtask

  task automatic accept_read_beat(
    input logic [1:0] data_id,
    input logic [127:0] data
  );
    integer cycles;
    begin
      response_data_in.ready = 1'b1;
      for (cycles = 0; cycles < 100 && !port_out.dat.response.valid; cycles = cycles + 1)
        tick();
      assert (port_out.dat.response.valid)
        else $fatal(1, "timed out waiting for fragmented read data");
      assert (port_out.dat.response.bits.opcode == COMP_DATA &&
              port_out.dat.response.bits.src_id == RAM_ID &&
              port_out.dat.response.bits.tgt_id == HOME_ID &&
              port_out.dat.response.bits.txn_id == 12'h700)
        else $fatal(1, "fragmented read routing metadata was incorrect");
      assert (port_out.dat.response.bits.data_id == data_id &&
              port_out.dat.response.bits.byte_enable == 16'hffff &&
              port_out.dat.response.bits.data == data)
        else $fatal(1, "fragmented read beat %0d was incorrect", data_id);
      tick();
      response_data_in.ready = 1'b0;
    end
  endtask

  initial begin
    requests_in = '0;
    responses_in = '0;
    request_data_in = '0;
    response_data_in = '0;
    tick();
    reset = 1'b0;

    write_line();

    issue_request(READ_NO_SNP, 12'h200, 44'h080000000, 6'd6, 12'h700);
    accept_read_beat(2'd0, 128'h00000000000000000000000000000000);
    accept_read_beat(2'd1, 128'h11111111111111111111111111111111);
    accept_read_beat(2'd2, 128'h22222222222222222222222222222222);
    accept_read_beat(2'd3, 128'h33333333333333333333333333333333);

    // Fragmenting the upper half of a line must preserve DataIDs 2 and 3.
    issue_request(READ_NO_SNP, 12'h201, 44'h080000020, 6'd5, 12'h700);
    accept_read_beat(2'd2, 128'h22222222222222222222222222222222);
    accept_read_beat(2'd3, 128'h33333333333333333333333333333333);

    write_word(44'h080000010, 12'h202, 128'hfedcba98765432100123456789abcdef);
    issue_request(READ_NO_SNP, 12'h203, 44'h080000010, 6'd4, 12'h700);
    accept_read_beat(2'd1, 128'hfedcba98765432100123456789abcdef);

    $display("CHI transfer fragmenter simulation passed");
    $finish;
  end
endmodule
