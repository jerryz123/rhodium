// Drives an out-of-range request into CHIRam to prove its address assertion.
// SPDX-License-Identifier: Apache-2.0
module chi_ram_invalid_tb;
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

  initial begin
    identity = '{node_id: 7'h09, base_address: 44'h080000000};
    requests_in = '0;
    request_data_in = '0;
    responses_in = '0;
    response_data_in = '0;
    tick();
    reset = 1'b0;

    requests_in.bits = '0;
    requests_in.bits.opcode = 7'h04;
    requests_in.bits.src_id = 7'h03;
    requests_in.bits.tgt_id = 7'h09;
    requests_in.bits.txn_id = 12'h301;
    requests_in.bits.address = 44'h080000040;
    requests_in.bits.size_or_num_req = 6'd4;
    requests_in.bits.return_nid_or_stash_nid_or_data_target = 7'h03;
    requests_in.bits.return_txn_id_or_stash_lpid = 12'h701;
    requests_in.valid = 1'b1;
    while (!requests_out.ready)
      tick();
    tick();

    $fatal(1, "out-of-range CHIRam request did not assert");
  end
endmodule
