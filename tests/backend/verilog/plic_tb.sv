// Verifies PLIC priorities, contexts, gateways, claim/completion, and CHI backpressure.
module plic_tb;
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
  localparam logic [6:0] PLIC_ID = 7'h0d;
  localparam logic [43:0] PLIC_BASE = 44'h0c000000;
  localparam logic [43:0] PRIORITY1 = PLIC_BASE + 44'h000004;
  localparam logic [43:0] PRIORITY2 = PLIC_BASE + 44'h000008;
  localparam logic [43:0] PRIORITY3 = PLIC_BASE + 44'h00000c;
  localparam logic [43:0] PENDING = PLIC_BASE + 44'h001000;
  localparam logic [43:0] ENABLE0 = PLIC_BASE + 44'h002000;
  localparam logic [43:0] ENABLE1 = PLIC_BASE + 44'h002080;
  localparam logic [43:0] THRESHOLD0 = PLIC_BASE + 44'h200000;
  localparam logic [43:0] CLAIM0 = PLIC_BASE + 44'h200004;
  localparam logic [43:0] THRESHOLD1 = PLIC_BASE + 44'h201000;
  localparam logic [43:0] CLAIM1 = PLIC_BASE + 44'h201004;

  logic clock = 1'b0;
  logic reset = 1'b1;
  identity_t identity;
  logic [2:0] sources;
  sn_in_t port_in;
  sn_out_t port_out;
  logic [1:0] context_interrupts;

  Plic dut (.*);
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
    input logic [11:0] return_txn_id
  );
    begin
      port_in.req.bits = '0;
      port_in.req.bits.opcode = opcode;
      port_in.req.bits.src_id = REQUESTER_ID;
      port_in.req.bits.tgt_id = PLIC_ID;
      port_in.req.bits.txn_id = txn_id;
      port_in.req.bits.address = address;
      port_in.req.bits.size_or_num_req = 6'd2;
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
      port_in.dat.request.bits.tgt_id = PLIC_ID;
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
        else $fatal(1, "PLIC DBID response did not survive backpressure");
      assert (port_out.rsp.response.bits.opcode == DBID_RESP &&
              port_out.rsp.response.bits.src_id == PLIC_ID &&
              port_out.rsp.response.bits.tgt_id == REQUESTER_ID &&
              port_out.rsp.response.bits.txn_id == request_txn_id &&
              port_out.rsp.response.bits.dbid_or_group_id == 0)
        else $fatal(1, "PLIC returned an invalid DBID response");
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
        else $fatal(1, "PLIC returned an invalid write completion");
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
        else $fatal(1, "PLIC read response did not survive backpressure");
      assert (port_out.dat.response.bits.opcode == COMP_DATA &&
              port_out.dat.response.bits.src_id == PLIC_ID &&
              port_out.dat.response.bits.tgt_id == REQUESTER_ID &&
              port_out.dat.response.bits.txn_id == return_txn_id &&
              port_out.dat.response.bits.byte_enable == byte_enable &&
              port_out.dat.response.bits.data == expected)
        else $fatal(1, "PLIC returned invalid read data");
      port_in.dat.response.ready = 1'b1;
      cycle();
      port_in.dat.response.ready = 1'b0;
    end
  endtask

  task automatic write32_opcode(
    input logic [6:0] opcode,
    input logic [11:0] txn_id,
    input logic [43:0] address,
    input logic [31:0] value
  );
    logic [3:0] lane;
    logic [15:0] byte_enable;
    logic [127:0] data;
    begin
      lane = address[3:0];
      byte_enable = 16'h000f << lane;
      data = {96'b0, value} << (lane * 8);
      issue_request(opcode, txn_id, address, 12'b0);
      accept_dbid(txn_id);
      issue_write_data(byte_enable, data);
      accept_comp(txn_id);
    end
  endtask

  task automatic write32(
    input logic [11:0] txn_id,
    input logic [43:0] address,
    input logic [31:0] value
  );
    write32_opcode(WRITE_NO_SNP_PTL, txn_id, address, value);
  endtask

  task automatic read32(
    input logic [11:0] txn_id,
    input logic [43:0] address,
    input logic [31:0] expected_value
  );
    logic [3:0] lane;
    logic [15:0] byte_enable;
    logic [127:0] expected_data;
    begin
      lane = address[3:0];
      byte_enable = 16'h000f << lane;
      expected_data = {96'b0, expected_value} << (lane * 8);
      issue_request(READ_NO_SNP, txn_id, address, txn_id + 12'h400);
      accept_read(txn_id + 12'h400, byte_enable, expected_data);
    end
  endtask

  initial begin
    identity = '{node_id: PLIC_ID, base_address: PLIC_BASE};
    sources = '0;
    port_in = '0;
    repeat (2) cycle();
    reset = 1'b0;
    assert (context_interrupts == 0)
      else $fatal(1, "PLIC reset asserted a context interrupt");
    read32(12'h100, PLIC_BASE, 32'h0);
    read32(12'h120, CLAIM0, 32'd0);
    read32(12'h121, CLAIM1, 32'd0);

    write32_opcode(WRITE_NO_SNP_FULL, 12'h101, PRIORITY1, 32'd3);
    write32(12'h102, PRIORITY2, 32'd3);
    write32(12'h103, PRIORITY3, 32'd5);
    sources = 3'b011;
    cycle();
    sources = '0;
    cycle();
    read32(12'h104, PENDING, 32'h00000006);
    assert (context_interrupts == 0)
      else $fatal(1, "disabled PLIC sources asserted an interrupt");

    write32(12'h105, ENABLE0, 32'h00000006);
    read32(12'h106, ENABLE0, 32'h00000006);
    assert (context_interrupts == 2'b01)
      else $fatal(1, "context zero did not observe its enabled source");
    write32(12'h107, THRESHOLD0, 32'd3);
    assert (context_interrupts == 0)
      else $fatal(1, "priority equal to threshold asserted an interrupt");
    write32(12'h108, THRESHOLD0, 32'd2);
    assert (context_interrupts == 2'b01)
      else $fatal(1, "priority above threshold did not assert");

    read32(12'h109, CLAIM0, 32'd1);
    read32(12'h10a, PENDING, 32'h00000004);
    assert (context_interrupts == 2'b01)
      else $fatal(1, "claim did not advance to the other tied source");
    read32(12'h10b, CLAIM0, 32'd2);
    assert (context_interrupts == 0)
      else $fatal(1, "claims did not clear pending state");
    write32(12'h10c, CLAIM0, 32'd1);
    write32(12'h10d, CLAIM0, 32'd2);

    write32(12'h10e, ENABLE1, 32'h00000008);
    write32(12'h10f, THRESHOLD1, 32'd5);
    sources = 3'b100;
    cycle();
    sources = '0;
    cycle();
    assert (context_interrupts == 0)
      else $fatal(1, "threshold did not suppress source three");
    read32(12'h110, CLAIM1, 32'd3);
    write32(12'h111, CLAIM0, 32'd3);
    sources = 3'b100;
    cycle();
    sources = '0;
    cycle();
    read32(12'h112, PENDING, 32'h0);

    sources = 3'b100;
    write32(12'h113, CLAIM1, 32'd3);
    sources = '0;
    cycle();
    read32(12'h114, PENDING, 32'h00000008);
    assert (context_interrupts == 0)
      else $fatal(1, "equal-threshold repend asserted an interrupt");
    write32(12'h115, THRESHOLD1, 32'd4);
    assert (context_interrupts == 2'b10)
      else $fatal(1, "completed level source did not repend");
    read32(12'h116, CLAIM1, 32'd3);
    write32(12'h117, CLAIM1, 32'd3);

    write32(12'h118, THRESHOLD0, 32'd0);
    write32(12'h119, THRESHOLD1, 32'd0);
    write32(12'h11a, ENABLE0, 32'h00000002);
    write32(12'h11b, ENABLE1, 32'h00000002);
    sources = 3'b001;
    cycle();
    assert (context_interrupts == 2'b11)
      else $fatal(1, "shared pending source did not reach both contexts");
    read32(12'h11c, CLAIM1, 32'd1);
    assert (context_interrupts == 0)
      else $fatal(1, "claim did not globally clear pending source state");
    // A held level cannot be claimed twice before its gateway is completed.
    read32(12'h122, CLAIM0, 32'd0);
    read32(12'h123, CLAIM1, 32'd0);
    sources = '0;
    cycle();
    write32(12'h11d, CLAIM1, 32'd1);
    read32(12'h124, CLAIM0, 32'd0);
    read32(12'h125, CLAIM1, 32'd0);

    $display("CHI-native PLIC priority, context, and gateway behavior passed");
    $finish;
  end
endmodule
