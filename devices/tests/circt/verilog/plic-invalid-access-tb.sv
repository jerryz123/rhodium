// Drives an unsupported eight-byte PLIC request and expects the device assertion to reject it.
// SPDX-License-Identifier: Apache-2.0
module plic_invalid_access_tb;
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
  localparam logic [6:0] REQUESTER_ID = 7'h03;
  localparam logic [6:0] PLIC_ID = 7'h0d;
  localparam logic [43:0] PLIC_BASE = 44'h0c000000;

  logic clock = 1'b0;
  logic reset = 1'b1;
  identity_t identity;
  logic [2:0] sources;
  sn_in_t port_in;
  sn_out_t port_out;
  logic [1:0] context_interrupts;

  Plic dut (.*);
  always #5 clock = ~clock;

  initial begin
    identity = '{node_id: PLIC_ID, base_address: PLIC_BASE};
    sources = '0;
    port_in = '0;
    repeat (2) @(posedge clock);
    reset = 1'b0;
    @(negedge clock);
    port_in.req.bits = '0;
    port_in.req.bits.opcode = READ_NO_SNP;
    port_in.req.bits.src_id = REQUESTER_ID;
    port_in.req.bits.tgt_id = PLIC_ID;
    port_in.req.bits.address = PLIC_BASE;
    port_in.req.bits.size_or_num_req = 6'd3;
    port_in.req.valid = 1'b1;
    @(posedge clock);
    #1;
    $fatal(1, "unsupported PLIC request did not trigger an assertion");
  end
endmodule
