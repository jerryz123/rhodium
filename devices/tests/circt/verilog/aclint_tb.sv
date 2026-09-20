// Verifies the CHI-native ACLINT timer, compare, software-interrupt, and masking behavior.
// SPDX-License-Identifier: Apache-2.0
module aclint_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_forward_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_forward_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_forward_t;
  typedef struct packed { logic valid; logic [63:0] bits; } valid_u64_t;
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
  localparam logic [6:0] ACLINT_ID = 7'h09;
  localparam logic [43:0] ACLINT_BASE = 44'h02000000;
  localparam logic [43:0] MSWI0 = ACLINT_BASE + 44'h0000;
  localparam logic [43:0] MSWI1 = ACLINT_BASE + 44'h0004;
  localparam logic [43:0] MTIMECMP0 = ACLINT_BASE + 44'h4000;
  localparam logic [43:0] MTIME = ACLINT_BASE + 44'hbff8;

  logic clock = 1'b0;
  logic reset = 1'b1;
  identity_t identity;
  logic tick;
  sn_in_t port_in;
  sn_out_t port_out;
  valid_u64_t time_update_out;
  logic [63:0] time_counter;
  logic [1:0] machine_software;
  logic [1:0] machine_timer;
  logic [3:0] time_update_count;
  logic [63:0] last_time_update;

  Aclint dut (.*);
  always #5 clock = ~clock;

  always_ff @(posedge clock) begin
    if (reset) begin
      time_update_count <= '0;
      last_time_update <= '0;
    end else if (time_update_out.valid) begin
      time_update_count <= time_update_count + 1'b1;
      last_time_update <= time_update_out.bits;
    end
  end

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
    input logic [5:0] size,
    input logic [11:0] return_txn_id
  );
    begin
      port_in.req.bits = '0;
      port_in.req.bits.opcode = opcode;
      port_in.req.bits.src_id = REQUESTER_ID;
      port_in.req.bits.tgt_id = ACLINT_ID;
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
    input logic [11:0] dbid,
    input logic [15:0] byte_enable,
    input logic [127:0] data
  );
    begin
      port_in.dat.request.bits = '0;
      port_in.dat.request.bits.opcode = NON_COPY_BACK_WRITE_DATA;
      port_in.dat.request.bits.src_id = REQUESTER_ID;
      port_in.dat.request.bits.tgt_id = ACLINT_ID;
      port_in.dat.request.bits.txn_id = dbid;
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
    begin
      port_in.rsp.response.ready = 1'b1;
      while (!port_out.rsp.response.valid)
        cycle();
      assert (port_out.rsp.response.bits.opcode == DBID_RESP &&
              port_out.rsp.response.bits.src_id == ACLINT_ID &&
              port_out.rsp.response.bits.tgt_id == REQUESTER_ID &&
              port_out.rsp.response.bits.txn_id == request_txn_id &&
              port_out.rsp.response.bits.dbid_or_group_id == 0)
        else $fatal(1, "ACLINT returned an invalid DBID response");
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
        else $fatal(1, "ACLINT returned an invalid write completion");
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
        else $fatal(1, "ACLINT read response did not survive backpressure");
      assert (port_out.dat.response.bits.opcode == COMP_DATA &&
              port_out.dat.response.bits.src_id == ACLINT_ID &&
              port_out.dat.response.bits.tgt_id == REQUESTER_ID &&
              port_out.dat.response.bits.txn_id == return_txn_id &&
              port_out.dat.response.bits.byte_enable == byte_enable &&
              port_out.dat.response.bits.data == expected)
        else $fatal(1, "ACLINT returned invalid read data");
      port_in.dat.response.ready = 1'b1;
      cycle();
      port_in.dat.response.ready = 1'b0;
    end
  endtask

  task automatic write_register(
    input logic [11:0] txn_id,
    input logic [43:0] address,
    input logic [5:0] size,
    input logic [15:0] byte_enable,
    input logic [127:0] data
  );
    begin
      issue_request(WRITE_NO_SNP_PTL, txn_id, address, size, 12'b0);
      accept_dbid(txn_id);
      issue_write_data(12'b0, byte_enable, data);
      accept_comp(txn_id);
    end
  endtask

  task automatic read_register(
    input logic [11:0] txn_id,
    input logic [43:0] address,
    input logic [5:0] size,
    input logic [15:0] byte_enable,
    input logic [127:0] expected
  );
    begin
      issue_request(READ_NO_SNP, txn_id, address, size, txn_id + 12'h400);
      accept_read(txn_id + 12'h400, byte_enable, expected);
    end
  endtask

  initial begin
    identity = '{node_id: ACLINT_ID, base_address: ACLINT_BASE};
    tick = 1'b0;
    port_in = '0;
    repeat (2) cycle();
    reset = 1'b0;
    assert (time_counter == 0 && machine_software == 0 && machine_timer == 0)
      else $fatal(1, "ACLINT reset state is incorrect");
    assert (!time_update_out.valid)
      else $fatal(1, "ACLINT emitted a time update without a timer event");

    tick = 1'b1;
    #1;
    assert (time_update_out.valid && time_update_out.bits == 1)
      else $fatal(1, "ACLINT did not announce the next timer value");
    cycle();
    tick = 1'b0;
    assert (time_counter == 1 && time_update_count == 1 && last_time_update == 1)
      else $fatal(1, "ACLINT tick did not emit the incremented mtime");

    write_register(12'h101, MTIME, 6'd3, 16'hff00, 128'h0);
    assert (time_update_count == 2 && last_time_update == 0)
      else $fatal(1, "ACLINT full mtime write did not emit its new value");
    write_register(12'h102, MTIME + 44'd4, 6'd2, 16'hf000,
                   128'h12345678_00000000_00000000_00000000);
    assert (time_update_count == 3 && last_time_update == 64'h12345678_00000000)
      else $fatal(1, "ACLINT partial mtime write did not emit its new value");
    read_register(12'h103, MTIME, 6'd3, 16'hff00,
                  128'h12345678_00000000_00000000_00000000);
    assert (time_update_count == 3)
      else $fatal(1, "ACLINT read emitted a spurious time update");

    write_register(12'h104, MTIMECMP0, 6'd3, 16'h00ff,
                   128'h12345678_00000002);
    assert (!machine_timer[0])
      else $fatal(1, "MTIP asserted before mtime reached mtimecmp");
    tick = 1'b1;
    repeat (2) cycle();
    tick = 1'b0;
    assert (machine_timer[0] && !machine_timer[1] &&
            time_update_count == 5 && last_time_update == 64'h12345678_00000002)
      else $fatal(1, "per-hart MTIP levels are incorrect");

    write_register(12'h105, MSWI1, 6'd2, 16'h0010, 128'h00000001_00000000);
    assert (machine_software == 2'b10)
      else $fatal(1, "MSIP write selected the wrong hart");
    read_register(12'h106, MSWI1, 6'd2, 16'h00f0, 128'h00000001_00000000);
    write_register(12'h107, MSWI1, 6'd2, 16'h0010, 128'h0);
    assert (machine_software == 0)
      else $fatal(1, "MSIP clear did not deassert the interrupt");
    read_register(12'h108, MSWI0, 6'd2, 16'h000f, 128'h0);

    // Shift the upper physical byte lanes into the register's local mask.
    write_register(12'h109, MTIME, 6'd3, 16'h8100,
                   128'hab000000_000000cd_00000000_00000000);
    assert (time_counter == 64'hab345678_000000cd &&
            time_update_count == 6 && last_time_update == time_counter)
      else $fatal(1, "sparse mask did not preserve unselected mtime bytes");
    write_register(12'h10a, MTIME, 6'd3, 16'h0000, '1);
    assert (time_counter == 64'hab345678_000000cd &&
            time_update_count == 7 && last_time_update == time_counter)
      else $fatal(1, "empty mask changed mtime or suppressed its write event");
    read_register(12'h10b, MTIME, 6'd3, 16'hff00,
                  128'hab345678_000000cd_00000000_00000000);

    $display("CHI-native ACLINT MTIMER and MSWI behavior passed");
    $finish;
  end
endmodule
