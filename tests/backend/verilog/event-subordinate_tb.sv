// Exercises repeated MMIO requests, response stalls, credit returns, and reset in every retained phase.
module event_subordinate_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_t;
  typedef struct packed {
    ready_t rsp;
    req_t req;
    struct packed { dat_t request; ready_t response; } dat;
  } sn_in_t;
  typedef struct packed {
    rsp_t rsp;
    ready_t req;
    struct packed { ready_t request; dat_t response; } dat;
  } sn_out_t;
  logic clock = 0, reset = 1, write_ready = 0;
  sn_in_t port_in;
  sn_out_t port_out;
  EventSubordinate dut (.*);
  always #5 clock = ~clock;
  import "DPI-C" function void event_subordinate_bind();
  import "DPI-C" function void event_subordinate_sample(int unsigned reset,
      int unsigned req_fire, int unsigned req_opcode, int unsigned req_txn,
      int unsigned rsp_fire, int unsigned rsp_opcode, int unsigned rsp_txn,
      int unsigned dat_fire, int unsigned dat_txn,
      int unsigned write_fire, int unsigned write_opcode, int unsigned stalled);
  import "DPI-C" function void event_subordinate_check();
  import "DPI-C" function void event_subordinate_finish();
  always @(posedge clock) begin
    event_subordinate_sample(int'(reset), int'(port_in.req.valid && port_out.req.ready),
        int'(port_in.req.bits.opcode), int'(port_in.req.bits.txn_id),
        int'(port_out.rsp.valid && port_in.rsp.ready), int'(port_out.rsp.bits.opcode), int'(port_out.rsp.bits.txn_id),
        int'(port_out.dat.response.valid && port_in.dat.response.ready), int'(port_out.dat.response.bits.txn_id),
        int'(port_in.dat.request.valid && port_out.dat.request.ready), int'(port_in.dat.request.bits.opcode),
        int'((port_out.rsp.valid && !port_in.rsp.ready) || (port_out.dat.response.valid && !port_in.dat.response.ready)));
    #1;
    event_subordinate_check();
  end
  task automatic tick;
    @(posedge clock); #2;
  endtask
  task automatic clear;
    reset = 1; port_in = '0; write_ready = 0;
    tick(); reset = 0; tick();
    assert (!port_out.rsp.valid && !port_out.dat.response.valid) else $fatal(1, "response survived reset");
  endtask
  task automatic credit;
    port_in.req = '0; port_in.req.valid = 1;
    port_in.dat.request = '0; port_in.dat.request.valid = 1;
    #1;
    assert (port_out.req.ready && port_out.dat.request.ready) else $fatal(1, "credit return blocked");
    tick(); port_in.req = '0; port_in.dat.request = '0;
  endtask
  task automatic request(input bit write_request);
    port_in.req = '0;
    port_in.req.bits.opcode = write_request ? 7'h1d : 7'h04;
    port_in.req.bits.txn_id = 12'habc;
    port_in.req.bits.src_id = 3; port_in.req.bits.tgt_id = 13;
    port_in.req.bits.return_nid_or_stash_nid_or_data_target = 3;
    port_in.req.bits.return_txn_id_or_stash_lpid = 12'habc;
    port_in.req.bits.size_or_num_req = 3;
    port_in.req.valid = 1;
    #1;
    assert (port_out.req.ready) else $fatal(1, "idle request blocked");
    tick(); port_in.req = '0;
  endtask
  task automatic accept_rsp(input logic [4:0] opcode);
    repeat (2) tick();
    assert (port_out.rsp.valid && port_out.rsp.bits.opcode == opcode && port_out.rsp.bits.txn_id == 12'habc)
      else $fatal(1, "incorrect RSP");
    port_in.rsp.ready = 1; tick(); port_in.rsp.ready = 0;
  endtask
  task automatic write_data;
    port_in.dat.request = '0;
    port_in.dat.request.bits.opcode = 3;
    port_in.dat.request.bits.src_id = 3; port_in.dat.request.bits.tgt_id = 13;
    port_in.dat.request.valid = 1;
    repeat (3) begin
      tick();
      assert (!port_out.dat.request.ready) else $fatal(1, "write readiness ignored");
    end
    write_ready = 1; tick(); write_ready = 0; port_in.dat.request = '0;
  endtask
  initial begin
    event_subordinate_bind();
    port_in = '0;
    clear();
    // Reset at ReadResponse, WriteDBID, WriteData, and WriteResponse.
    for (int phase = 0; phase < 4; ++phase) begin
      request(phase != 0);
      if (phase >= 2) accept_rsp(6);
      if (phase == 3) write_data();
      credit(); clear();
    end
    // Reuse exactly the same IDs and payloads across distinct occurrences.
    repeat (4) begin
      credit(); request(0); credit();
      repeat (3) tick();
      assert (port_out.dat.response.valid && port_out.dat.response.bits.opcode == 4 &&
          port_out.dat.response.bits.txn_id == 12'habc && port_out.dat.response.bits.data == 128'd42)
        else $fatal(1, "incorrect read completion");
      port_in.dat.response.ready = 1; tick(); port_in.dat.response.ready = 0;
      request(1); credit(); accept_rsp(6); credit(); write_data(); credit(); accept_rsp(4);
    end
    tick(); event_subordinate_finish(); $finish;
  end
  initial begin
    #20000; $fatal(1, "subordinate trace timeout");
  end
endmodule
