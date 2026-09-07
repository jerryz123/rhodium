// Verifies the CHI UART register contract, FIFOs, interrupts, and serial pins.
module uart16550_tb;
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
  localparam logic [6:0] WRITE_NO_SNP_PTL = 7'h1c;
  localparam logic [4:0] COMP = 5'h04;
  localparam logic [4:0] DBID_RESP = 5'h06;
  localparam logic [3:0] NON_COPY_BACK_WRITE_DATA = 4'h3;
  localparam logic [3:0] COMP_DATA = 4'h4;
  localparam logic [6:0] REQUESTER_ID = 7'h03;
  localparam logic [6:0] UART_ID = 7'h0b;
  localparam logic [43:0] UART_BASE = 44'h10000000;

  logic clock = 1'b0;
  logic reset = 1'b1;
  identity_t identity;
  logic rx;
  logic tx;
  logic interrupt;
  sn_in_t port_in;
  sn_out_t port_out;

  Uart16550 dut (.*);
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
    input logic [2:0] offset,
    input logic [11:0] return_txn_id
  );
    begin
      port_in.req.bits = '0;
      port_in.req.bits.opcode = opcode;
      port_in.req.bits.src_id = REQUESTER_ID;
      port_in.req.bits.tgt_id = UART_ID;
      port_in.req.bits.txn_id = txn_id;
      port_in.req.bits.address = UART_BASE + {{41{1'b0}}, offset};
      port_in.req.bits.size_or_num_req = 0;
      port_in.req.bits.return_nid_or_stash_nid_or_data_target = REQUESTER_ID;
      port_in.req.bits.return_txn_id_or_stash_lpid = return_txn_id;
      port_in.req.valid = 1'b1;
      while (!port_out.req.ready)
        cycle();
      cycle();
      port_in.req = '0;
    end
  endtask

  task automatic issue_write_data(input logic [2:0] offset, input logic [7:0] value);
    logic [127:0] payload;
    logic [15:0] byte_enable;
    begin
      payload = '0;
      payload[offset * 8 +: 8] = value;
      byte_enable = 16'h1 << offset;
      port_in.dat.request.bits = '0;
      port_in.dat.request.bits.opcode = NON_COPY_BACK_WRITE_DATA;
      port_in.dat.request.bits.src_id = REQUESTER_ID;
      port_in.dat.request.bits.tgt_id = UART_ID;
      port_in.dat.request.bits.txn_id = 0;
      port_in.dat.request.bits.byte_enable = byte_enable;
      port_in.dat.request.bits.data = payload;
      port_in.dat.request.valid = 1'b1;
      while (!port_out.dat.request.ready)
        cycle();
      cycle();
      port_in.dat.request = '0;
    end
  endtask

  task automatic accept_dbid(input logic [11:0] request_txn_id);
    begin
      port_in.rsp.response.ready = 1'b1;
      while (!port_out.rsp.response.valid)
        cycle();
      assert (port_out.rsp.response.bits.opcode == DBID_RESP &&
              port_out.rsp.response.bits.src_id == UART_ID &&
              port_out.rsp.response.bits.tgt_id == REQUESTER_ID &&
              port_out.rsp.response.bits.txn_id == request_txn_id &&
              port_out.rsp.response.bits.dbid_or_group_id == 0)
        else $fatal(1, "UART returned an invalid DBID response");
      cycle();
      port_in.rsp.response.ready = 1'b0;
    end
  endtask

  task automatic accept_comp(input logic [11:0] request_txn_id);
    begin
      port_in.rsp.response.ready = 1'b1;
      while (!port_out.rsp.response.valid)
        cycle();
      assert (port_out.rsp.response.bits.opcode == COMP &&
              port_out.rsp.response.bits.txn_id == request_txn_id)
        else $fatal(1, "UART returned an invalid write completion");
      cycle();
      port_in.rsp.response.ready = 1'b0;
    end
  endtask

  task automatic write_register(
    input logic [11:0] txn_id,
    input logic [2:0] offset,
    input logic [7:0] value
  );
    begin
      issue_request(WRITE_NO_SNP_PTL, txn_id, offset, 0);
      accept_dbid(txn_id);
      issue_write_data(offset, value);
      accept_comp(txn_id);
    end
  endtask

  task automatic read_register(
    input logic [11:0] txn_id,
    input logic [2:0] offset,
    input logic [7:0] expected
  );
    logic [127:0] held_data;
    logic [15:0] expected_enable;
    begin
      issue_request(READ_NO_SNP, txn_id, offset, txn_id + 12'h400);
      while (!port_out.dat.response.valid)
        cycle();
      held_data = port_out.dat.response.bits.data;
      cycle();
      assert (port_out.dat.response.valid && port_out.dat.response.bits.data == held_data)
        else $fatal(1, "UART read response did not survive backpressure");
      expected_enable = 16'h1 << offset;
      assert (port_out.dat.response.bits.opcode == COMP_DATA &&
              port_out.dat.response.bits.src_id == UART_ID &&
              port_out.dat.response.bits.tgt_id == REQUESTER_ID &&
              port_out.dat.response.bits.txn_id == txn_id + 12'h400 &&
              port_out.dat.response.bits.byte_enable == expected_enable &&
              port_out.dat.response.bits.data[offset * 8 +: 8] == expected)
        else $fatal(1,
                    "UART returned 0x%02x instead of 0x%02x at offset %0d",
                    port_out.dat.response.bits.data[offset * 8 +: 8],
                    expected,
                    offset);
      port_in.dat.response.ready = 1'b1;
      cycle();
      port_in.dat.response.ready = 1'b0;
    end
  endtask

  task automatic expect_tx_byte(input logic [7:0] expected, input integer clocks_per_bit);
    integer index;
    begin
      while (tx !== 1'b0)
        cycle();
      repeat (clocks_per_bit / 2) cycle();
      assert (tx == 1'b0)
        else $fatal(1, "UART TX start bit was not low");
      for (index = 0; index < 8; index = index + 1) begin
        repeat (clocks_per_bit) cycle();
        assert (tx == expected[index])
          else $fatal(1, "UART TX data bit %0d was incorrect", index);
      end
      repeat (clocks_per_bit) cycle();
      assert (tx == 1'b1)
        else $fatal(1, "UART TX stop bit was not high");
      repeat (clocks_per_bit / 2) cycle();
    end
  endtask

  task automatic send_rx_byte(
    input logic [7:0] value,
    input integer clocks_per_bit,
    input logic valid_stop
  );
    integer index;
    begin
      rx = 1'b0;
      repeat (clocks_per_bit) cycle();
      for (index = 0; index < 8; index = index + 1) begin
        rx = value[index];
        repeat (clocks_per_bit) cycle();
      end
      rx = valid_stop;
      repeat (clocks_per_bit) cycle();
      rx = 1'b1;
      repeat (4) cycle();
    end
  endtask

  initial begin
    identity = '{node_id: UART_ID, base_address: UART_BASE};
    rx = 1'b1;
    port_in = '0;
    repeat (4) cycle();
    reset = 1'b0;
    repeat (4) cycle();
    assert (tx == 1'b1 && !interrupt)
      else $fatal(1, "UART reset outputs are incorrect");

    read_register(12'h101, 3'd5, 8'h60);
    write_register(12'h102, 3'd7, 8'ha5);
    read_register(12'h103, 3'd7, 8'ha5);

    write_register(12'h104, 3'd3, 8'h83);
    write_register(12'h105, 3'd0, 8'h02);
    write_register(12'h106, 3'd1, 8'h00);
    read_register(12'h107, 3'd0, 8'h02);
    read_register(12'h108, 3'd1, 8'h00);
    write_register(12'h109, 3'd3, 8'h03);
    write_register(12'h10a, 3'd2, 8'h07);
    read_register(12'h10b, 3'd2, 8'hc1);

    fork
      expect_tx_byte(8'ha6, 32);
      write_register(12'h10c, 3'd0, 8'ha6);
    join
    read_register(12'h10d, 3'd5, 8'h60);

    // Clearing queued TX data must not abort the active byte or start a stale byte.
    fork
      expect_tx_byte(8'h96, 32);
      begin
        write_register(12'h120, 3'd0, 8'h96);
        write_register(12'h121, 3'd0, 8'h69);
        write_register(12'h122, 3'd2, 8'h05);
      end
    join
    repeat (352) begin
      cycle();
      assert (tx) else $fatal(1, "cleared TX FIFO transmitted a stale byte");
    end
    read_register(12'h123, 3'd5, 8'h60);

    write_register(12'h10e, 3'd1, 8'h01);
    send_rx_byte(8'h3c, 32, 1'b1);
    assert (interrupt)
      else $fatal(1, "UART receive interrupt did not assert");
    read_register(12'h10f, 3'd5, 8'h61);
    read_register(12'h110, 3'd2, 8'hc4);
    read_register(12'h111, 3'd0, 8'h3c);
    assert (!interrupt)
      else $fatal(1, "UART receive interrupt did not clear after RBR read");
    read_register(12'h112, 3'd5, 8'h60);

    send_rx_byte(8'h12, 32, 1'b1);
    send_rx_byte(8'h34, 32, 1'b1);
    read_register(12'h113, 3'd0, 8'h12);
    read_register(12'h114, 3'd0, 8'h34);

    send_rx_byte(8'h5a, 32, 1'b1);
    write_register(12'h115, 3'd2, 8'h03);
    read_register(12'h116, 3'd5, 8'h60);

    send_rx_byte(8'h81, 32, 1'b0);
    read_register(12'h117, 3'd5, 8'h69);
    read_register(12'h118, 3'd0, 8'h81);
    read_register(12'h119, 3'd5, 8'h60);
    repeat (32) cycle();

    write_register(12'h11a, 3'd2, 8'h00);
    send_rx_byte(8'haa, 32, 1'b1);
    read_register(12'h11b, 3'd5, 8'h61);
    send_rx_byte(8'h55, 32, 1'b1);
    read_register(12'h11c, 3'd5, 8'h63);
    read_register(12'h11d, 3'd0, 8'haa);
    read_register(12'h11e, 3'd5, 8'h60);

    $display("CHI UART register, FIFO, interrupt, and serial behavior passed");
    $finish;
  end
endmodule
