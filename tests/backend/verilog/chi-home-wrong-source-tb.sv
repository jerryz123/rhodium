// Proves that HN-I rejects response and read-data traffic from the wrong subordinate.
// SPDX-License-Identifier: Apache-2.0
package chi_home_wrong_source_types;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_t;
  typedef struct packed {
    req_t req;
    struct packed { rsp_t requester; ready_t response; } rsp;
    struct packed { dat_t request; ready_t response; } dat;
  } requester_in_t;
  typedef struct packed {
    ready_t req;
    struct packed { ready_t requester; rsp_t response; } rsp;
    struct packed { ready_t request; dat_t response; } dat;
  } requester_out_t;
  typedef struct packed {
    struct packed { rsp_t response; } rsp;
    ready_t req;
    struct packed { ready_t request; dat_t response; } dat;
  } subordinate_in_t;
  typedef struct packed {
    struct packed { ready_t response; } rsp;
    req_t req;
    struct packed { dat_t request; ready_t response; } dat;
  } subordinate_out_t;
  typedef struct packed { requester_in_t requester; subordinate_in_t subordinate; } hni_in_t;
  typedef struct packed { requester_out_t requester; subordinate_out_t subordinate; } hni_out_t;
endpackage

module chi_home_wrong_response_source_tb;
  import chi_home_wrong_source_types::*;
  logic clock = 1'b0;
  logic reset = 1'b1;
  hni_in_t port_in;
  hni_out_t port_out;
  logic [11:0] slot;

  CHIHNI dut (.*);
  always #5 clock = ~clock;

  task automatic tick;
    begin
      @(posedge clock);
      #1;
    end
  endtask

  initial begin
    port_in = '0;
    tick();
    reset = 1'b0;
    port_in.subordinate.req.ready = 1'b1;
    port_in.requester.req.bits.opcode = 7'h1d;
    port_in.requester.req.bits.src_id = 7'h03;
    port_in.requester.req.bits.tgt_id = 7'h05;
    port_in.requester.req.bits.txn_id = 12'h101;
    port_in.requester.req.bits.address = 44'h080000000;
    port_in.requester.req.bits.size_or_num_req = 6'd4;
    port_in.requester.req.valid = 1'b1;
    #1;
    while (!port_out.requester.req.ready)
      tick();
    slot = port_out.subordinate.req.bits.txn_id;
    tick();
    port_in.requester.req = '0;
    port_in.requester.rsp.response.ready = 1'b1;
    port_in.subordinate.rsp.response.bits.opcode = 5'h06;
    port_in.subordinate.rsp.response.bits.src_id = 7'h0a;
    port_in.subordinate.rsp.response.bits.tgt_id = 7'h05;
    port_in.subordinate.rsp.response.bits.txn_id = slot;
    port_in.subordinate.rsp.response.valid = 1'b1;
    tick();
    $fatal(1, "wrong-source DBID response was accepted");
  end
endmodule

module chi_home_wrong_data_source_tb;
  import chi_home_wrong_source_types::*;
  logic clock = 1'b0;
  logic reset = 1'b1;
  hni_in_t port_in;
  hni_out_t port_out;
  logic [11:0] slot;

  CHIHNI dut (.*);
  always #5 clock = ~clock;

  task automatic tick;
    begin
      @(posedge clock);
      #1;
    end
  endtask

  initial begin
    port_in = '0;
    tick();
    reset = 1'b0;
    port_in.subordinate.req.ready = 1'b1;
    port_in.requester.req.bits.opcode = 7'h04;
    port_in.requester.req.bits.src_id = 7'h03;
    port_in.requester.req.bits.tgt_id = 7'h05;
    port_in.requester.req.bits.txn_id = 12'h102;
    port_in.requester.req.bits.return_nid_or_stash_nid_or_data_target = 7'h03;
    port_in.requester.req.bits.return_txn_id_or_stash_lpid = 12'h502;
    port_in.requester.req.bits.address = 44'h080000000;
    port_in.requester.req.bits.size_or_num_req = 6'd4;
    port_in.requester.req.valid = 1'b1;
    #1;
    while (!port_out.requester.req.ready)
      tick();
    slot = port_out.subordinate.req.bits.txn_id;
    tick();
    port_in.requester.req = '0;
    port_in.requester.dat.response.ready = 1'b1;
    port_in.subordinate.dat.response.bits.opcode = 4'h04;
    port_in.subordinate.dat.response.bits.src_id = 7'h0a;
    port_in.subordinate.dat.response.bits.tgt_id = 7'h05;
    port_in.subordinate.dat.response.bits.txn_id = slot;
    port_in.subordinate.dat.response.bits.data_id = 2'd0;
    port_in.subordinate.dat.response.valid = 1'b1;
    tick();
    $fatal(1, "wrong-source read data was accepted");
  end
endmodule
