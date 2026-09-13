// Verifies CHI BootROM reads, byte lanes, backpressure, and top-of-address-space containment.
// SPDX-License-Identifier: Apache-2.0
module bootrom_tb;
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
  localparam logic [3:0] COMP_DATA = 4'h4;
  localparam logic [6:0] REQUESTER_ID = 7'h03;
  localparam logic [6:0] BOOTROM_ID = 7'h0c;
  localparam logic [43:0] BOOTROM_BASE = 44'h00010000;
  localparam logic [127:0] BEAT_0 = 128'hff858593_ffff0597_00051a63_f1402573;
  localparam logic [127:0] BEAT_1 = 128'hffdff06f_10500073_ff028067_7fff0297;

  logic clock = 1'b0;
  logic reset = 1'b1;
  identity_t identity;
  sn_in_t port_in;
  sn_out_t port_out;

  CHIBootROM dut (.*);
  always #5 clock = ~clock;

  task automatic cycle;
    begin
      @(posedge clock);
      #1;
    end
  endtask

  task automatic issue_read(
    input logic [11:0] txn_id,
    input logic [43:0] address,
    input logic [5:0] size,
    input logic [11:0] return_txn_id
  );
    begin
      port_in.req.bits = '0;
      port_in.req.bits.opcode = READ_NO_SNP;
      port_in.req.bits.src_id = REQUESTER_ID;
      port_in.req.bits.tgt_id = BOOTROM_ID;
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

  task automatic check_response(
    input logic [1:0] data_id,
    input logic [11:0] return_txn_id,
    input logic [15:0] byte_enable,
    input logic [127:0] expected
  );
    begin
      while (!port_out.dat.response.valid)
        cycle();
      assert (port_out.dat.response.bits.opcode == COMP_DATA &&
              port_out.dat.response.bits.src_id == BOOTROM_ID &&
              port_out.dat.response.bits.tgt_id == REQUESTER_ID &&
              port_out.dat.response.bits.txn_id == return_txn_id &&
              port_out.dat.response.bits.data_id == data_id &&
              port_out.dat.response.bits.byte_enable == byte_enable &&
              port_out.dat.response.bits.data == expected)
        else $fatal(1, "BootROM returned invalid read data");
      cycle();
    end
  endtask

  initial begin
    identity = '{node_id: BOOTROM_ID, base_address: BOOTROM_BASE};
    port_in = '0;
    repeat (2) cycle();
    reset = 1'b0;

    issue_read(12'h101, BOOTROM_BASE, 6'd6, 12'h501);
    while (!port_out.dat.response.valid)
      cycle();
    assert (port_out.dat.response.bits.data == BEAT_0)
      else $fatal(1, "BootROM first response beat is incorrect");
    cycle();
    assert (port_out.dat.response.valid &&
            port_out.dat.response.bits.data_id == 0 &&
            port_out.dat.response.bits.data == BEAT_0)
      else $fatal(1, "BootROM response did not survive backpressure");

    port_in.dat.response.ready = 1'b1;
    check_response(2'd0, 12'h501, 16'hffff, BEAT_0);
    check_response(2'd1, 12'h501, 16'hffff, BEAT_1);
    check_response(2'd2, 12'h501, 16'hffff, 128'b0);
    check_response(2'd3, 12'h501, 16'hffff, 128'b0);
    port_in.dat.response.ready = 1'b0;

    issue_read(12'h102, BOOTROM_BASE + 44'd4, 6'd2, 12'h502);
    port_in.dat.response.ready = 1'b1;
    check_response(2'd0, 12'h502, 16'h00f0, BEAT_0);
    port_in.dat.response.ready = 1'b0;

    issue_read(12'h103, BOOTROM_BASE + 44'd16, 6'd2, 12'h503);
    port_in.dat.response.ready = 1'b1;
    check_response(2'd1, 12'h503, 16'h000f, BEAT_1);
    port_in.dat.response.ready = 1'b0;

    assert (!port_out.rsp.response.valid)
      else $fatal(1, "read-only BootROM unexpectedly emitted a response flit");
    // The 64-byte window and final transfer end exactly at 2^44, without wrapping.
    identity.base_address = 44'hfffffffffc0;
    issue_read(12'h104, 44'hffffffffff0, 6'd4, 12'h504);
    port_in.dat.response.ready = 1'b1;
    check_response(2'd3, 12'h504, 16'hffff, 128'b0);
    port_in.dat.response.ready = 1'b0;
    port_in.req.bits.opcode = READ_NO_SNP;
    port_in.req.bits.tgt_id = BOOTROM_ID;
    port_in.req.bits.size_or_num_req = 6'd4;
    port_in.req.bits.address = 44'hfffffffffb0;
    port_in.req.valid = 1'b1;
    // Inspect the combinational refusal without clocking a protocol violation.
    #1;
    assert (!port_out.req.ready) else $fatal(1, "accepted read below top window");
    port_in.req = '0;
    $display("CHI BootROM read and backpressure behavior passed");
    $finish;
  end
endmodule
