// Verifies boot-address reset, word/doubleword access, byte masks, and CHI backpressure.
module boot_address_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_forward_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_forward_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_forward_t;
  typedef struct packed {
    struct packed { ready_t response; } rsp;
    req_forward_t req;
    struct packed { dat_forward_t request; ready_t response; } dat;
  } sn_in_t;
  typedef struct packed {
    struct packed { rsp_forward_t response; } rsp;
    ready_t req;
    struct packed { ready_t request; dat_forward_t response; } dat;
  } sn_out_t;
  typedef struct packed { logic [6:0] node_id; logic [43:0] base_address; } identity_t;

  localparam logic [6:0] READ_NO_SNP = 7'h04;
  localparam logic [6:0] WRITE_NO_SNP_FULL = 7'h1d;
  localparam logic [6:0] WRITE_NO_SNP_PTL = 7'h1c;
  localparam logic [4:0] COMP = 5'h04;
  localparam logic [4:0] DBID_RESP = 5'h06;
  localparam logic [3:0] NON_COPY_BACK_WRITE_DATA = 4'h3;
  localparam logic [3:0] COMP_DATA = 4'h4;
  localparam logic [6:0] REQUESTER_ID = 7'h03;
  localparam logic [6:0] BOOT_ID = 7'h0d;
  localparam logic [43:0] BOOT_BASE = 44'h00001000;
  logic clock = 1'b0;
  logic reset = 1'b1;
  identity_t identity;
  sn_in_t port_in;
  sn_out_t port_out;

  CHIBootAddressRegister dut (.*);
  always #5 clock = ~clock;

  task automatic cycle;
    begin
      @(posedge clock);
      #1;
    end
  endtask

  task automatic issue_request(
    input logic [6:0] opcode,
    input logic [11:0] txn_id,
    input logic [43:0] address,
    input logic [11:0] return_txn_id,
    input logic [5:0] size
  );
    begin
      port_in.req.bits = '0;
      port_in.req.bits.opcode = opcode;
      port_in.req.bits.src_id = REQUESTER_ID;
      port_in.req.bits.tgt_id = BOOT_ID;
      port_in.req.bits.txn_id = txn_id;
      port_in.req.bits.address = address;
      port_in.req.bits.size_or_num_req = size;
      port_in.req.bits.return_nid_or_stash_nid_or_data_target = REQUESTER_ID;
      port_in.req.bits.return_txn_id_or_stash_lpid = return_txn_id;
      port_in.req.valid = 1'b1;
      while (!port_out.req.ready)
        cycle();
      cycle();
      port_in.req = '0;
    end
  endtask

  task automatic issue_write_data(
    input logic [15:0] byte_enable,
    input logic [127:0] data
  );
    begin
      port_in.dat.request.bits = '0;
      port_in.dat.request.bits.opcode = NON_COPY_BACK_WRITE_DATA;
      port_in.dat.request.bits.src_id = REQUESTER_ID;
      port_in.dat.request.bits.tgt_id = BOOT_ID;
      port_in.dat.request.bits.txn_id = 12'b0;
      port_in.dat.request.bits.byte_enable = byte_enable;
      port_in.dat.request.bits.data = data;
      port_in.dat.request.valid = 1'b1;
      while (!port_out.dat.request.ready)
        cycle();
      cycle();
      port_in.dat.request = '0;
    end
  endtask

  task automatic accept_dbid(input logic [11:0] request_txn_id);
    logic [11:0] held_txn_id;
    begin
      while (!port_out.rsp.response.valid)
        cycle();
      held_txn_id = port_out.rsp.response.bits.txn_id;
      cycle();
      assert (port_out.rsp.response.valid && port_out.rsp.response.bits.txn_id == held_txn_id)
        else $fatal(1, "BOOT DBID response did not survive backpressure");
      assert (port_out.rsp.response.bits.opcode == DBID_RESP &&
              port_out.rsp.response.bits.src_id == BOOT_ID &&
              port_out.rsp.response.bits.tgt_id == REQUESTER_ID &&
              port_out.rsp.response.bits.txn_id == request_txn_id &&
              port_out.rsp.response.bits.dbid_or_group_id == 0)
        else $fatal(1, "BOOT returned an invalid DBID response");
      port_in.rsp.response.ready = 1'b1;
      cycle();
      port_in.rsp.response.ready = 1'b0;
    end
  endtask

  task automatic accept_comp(input logic [11:0] request_txn_id);
    begin
      while (!port_out.rsp.response.valid)
        cycle();
      assert (port_out.rsp.response.bits.opcode == COMP &&
              port_out.rsp.response.bits.txn_id == request_txn_id)
        else $fatal(1, "BOOT returned an invalid write completion");
      port_in.rsp.response.ready = 1'b1;
      cycle();
      port_in.rsp.response.ready = 1'b0;
    end
  endtask

  task automatic accept_read(
    input logic [11:0] return_txn_id,
    input logic [15:0] byte_enable,
    input logic [127:0] expected
  );
    logic [127:0] held_data;
    begin
      while (!port_out.dat.response.valid)
        cycle();
      held_data = port_out.dat.response.bits.data;
      cycle();
      assert (port_out.dat.response.valid && port_out.dat.response.bits.data == held_data)
        else $fatal(1, "BOOT read response did not survive backpressure");
      assert (port_out.dat.response.bits.opcode == COMP_DATA &&
              port_out.dat.response.bits.src_id == BOOT_ID &&
              port_out.dat.response.bits.tgt_id == REQUESTER_ID &&
              port_out.dat.response.bits.txn_id == return_txn_id &&
              port_out.dat.response.bits.byte_enable == byte_enable &&
              port_out.dat.response.bits.data == expected)
        else $fatal(1, "BOOT returned invalid read data");
      port_in.dat.response.ready = 1'b1;
      cycle();
      port_in.dat.response.ready = 1'b0;
    end
  endtask

  task automatic write_address(input logic [43:0] address, input logic [5:0] size,
                               input logic [15:0] mask, input logic [127:0] value,
                               input logic [6:0] opcode = WRITE_NO_SNP_PTL);
    issue_request(opcode, 12'habc, address, 0, size);
    accept_dbid(12'habc);
    repeat (3) cycle();
    issue_write_data(mask, value);
    // A stalled completion must not accept a new ordinary request.
    port_in.req.bits = '0;
    port_in.req.bits.opcode = READ_NO_SNP;
    port_in.req.bits.tgt_id = BOOT_ID;
    port_in.req.bits.address = BOOT_BASE;
    port_in.req.bits.size_or_num_req = 3;
    port_in.req.valid = 1;
    repeat (3) begin
      cycle();
      assert (!port_out.req.ready) else $fatal(1, "overlapping boot-register transaction");
    end
    accept_comp(12'habc);
    port_in.req = '0;
  endtask

  task automatic read_address(input logic [43:0] address, input logic [5:0] size,
                              input logic [15:0] mask, input logic [127:0] value);
    issue_request(READ_NO_SNP, 12'h123, address, 12'h456, size);
    accept_read(12'h456, mask, value);
  endtask

  initial begin
    identity = '{node_id: BOOT_ID, base_address: BOOT_BASE};
    port_in = '0;
    repeat (3) cycle();
    reset = 0;
    cycle();
    read_address(BOOT_BASE, 3, 16'h00ff, 128'h1234567880000000);
    read_address(BOOT_BASE, 2, 16'h000f, 128'h1234567880000000);
    read_address(BOOT_BASE + 4, 2, 16'h00f0, 128'h1234567880000000);
    write_address(BOOT_BASE, 2, 16'h000f, 128'hdeadbeef, WRITE_NO_SNP_FULL);
    read_address(BOOT_BASE, 3, 16'h00ff, 128'h12345678deadbeef);
    write_address(BOOT_BASE + 4, 2, 16'h00f0, 128'hfedcba9800000000, WRITE_NO_SNP_FULL);
    read_address(BOOT_BASE, 3, 16'h00ff, 128'hfedcba98deadbeef);
    write_address(BOOT_BASE, 3, 16'h0081, 128'h1100000000000022);
    read_address(BOOT_BASE, 3, 16'h00ff, 128'h11dcba98deadbe22);
    write_address(BOOT_BASE, 3, 16'h0000, '1);
    read_address(BOOT_BASE, 3, 16'h00ff, 128'h11dcba98deadbe22);
    write_address(BOOT_BASE, 3, 16'h00ff, 128'h0000000180002000, WRITE_NO_SNP_FULL);
    read_address(BOOT_BASE, 3, 16'h00ff, 128'h0000000180002000);
    // Credit-return flits are accepted without changing the register.
    port_in.req.valid = 1;
    port_in.req.bits = '0;
    port_in.dat.request.valid = 1;
    port_in.dat.request.bits = '0;
    cycle();
    port_in = '0;
    read_address(BOOT_BASE, 3, 16'h00ff, 128'h0000000180002000);
    reset = 1;
    cycle();
    reset = 0;
    cycle();
    read_address(BOOT_BASE, 3, 16'h00ff, 128'h1234567880000000);
    $display("boot-address passed");
    $finish;
  end

  initial begin
    #100000;
    $fatal(1, "boot-address timeout");
  end
endmodule
