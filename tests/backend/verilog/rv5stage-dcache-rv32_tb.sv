// Checks RV32 64-byte block operations, bounded LR/SC reservations, and NTL loads.
module rv5stage_dcache_rv32_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; RV5StageDataReq bits; } request_t;
  typedef struct packed { logic valid; RV5StageDataResp bits; } response_t;
  typedef struct packed { request_t request; } core_in_t;
  typedef struct packed { ready_t request; logic request_fault; logic request_access_fault; response_t response; logic drained; logic reservation_valid; } core_out_t;
  typedef struct packed { logic valid; CachePrefetchReq bits; } prefetch_t;
  typedef struct packed { logic valid; CHIReqFlit bits; } req_t;
  typedef struct packed { logic valid; CHIRspFlit bits; } rsp_t;
  typedef struct packed { logic valid; CHIDatFlit bits; } dat_t;
  typedef struct packed { logic valid; CHISnpFlit bits; } snp_t;
  typedef struct packed { ready_t requests; ready_t requester_responses; ready_t request_data; rsp_t responses; dat_t response_data; snp_t snoops; } chi_in_t;
  typedef struct packed { req_t requests; rsp_t requester_responses; dat_t request_data; ready_t responses; ready_t response_data; ready_t snoops; } chi_out_t;

  logic clock = 0;
  logic reset = 1;
  logic [6:0] node_id = 7'd3;
  core_in_t core_in;
  core_out_t core_out;
  prefetch_t prefetch_in;
  typedef struct packed { logic valid; logic [31:0] bits; } lookup_t;
  lookup_t virtual_lookup_in;
  assign virtual_lookup_in = {core_in.request.valid, core_in.request.bits.address ^ 32'h4000_0000};
  chi_in_t chi_in;
  chi_out_t chi_out;
  integer requests = 0;
  integer responses = 0;
  integer acknowledgements = 0;
  logic [6:0] expected_opcode = 7'h07;
  logic [43:0] expected_address = 44'h1000;
  RV5StageL1DCache dut (.*);

  task automatic tick;
    if (!reset) begin
      if (chi_out.requests.valid && chi_in.requests.ready) begin
        assert (chi_out.requests.bits.opcode == expected_opcode && chi_out.requests.bits.address == expected_address && chi_out.requests.bits.size_or_num_req == 6)
          else $fatal(1, "RV32 zero did not acquire the aligned block");
        requests++;
      end
      if (chi_out.requester_responses.valid && chi_in.requester_responses.ready) acknowledgements++;
      if (core_out.response.valid) responses++;
    end
    #5 clock = 1;
    #1 clock = 0;
    #4;
  endtask

  task automatic send_request(input logic [31:0] address, input logic [3:0] access,
                              input logic [2:0] locality = 0,
                              input logic [1:0] size = 2,
                              input logic unsigned_load = 0,
                              input logic [31:0] data = 0);
    for (int cycle = 0; cycle < 100 && !core_out.request.ready; cycle++) tick();
    assert (core_out.request.ready) else $fatal(1, "RV32 request timeout");
    core_in.request.bits = '0;
    core_in.request.bits.address = address;
    core_in.request.bits.access = access;
    core_in.request.bits.width = size;
    core_in.request.bits.unsigned_0 = unsigned_load;
    core_in.request.bits.locality = locality;
    core_in.request.bits.data = data;
    core_in.request.bits.destination = access inside {4'd1, 4'd3, 4'd4} ? 2'd1 : 2'd0;
    core_in.request.valid = 1;
    tick();
    core_in.request.valid = 0;
  endtask

  task automatic expect_response(input logic [31:0] expected);
    for (int cycle = 0; cycle < 100 && !core_out.response.valid; cycle++) tick();
    assert (core_out.response.valid && core_out.response.bits.data == expected)
      else $fatal(1, "RV32 zero/load response mismatch");
    tick();
  endtask

  initial begin
    core_in = '0;
    prefetch_in = '0;
    chi_in = '0;
    repeat (2) tick();
    reset = 0;
    chi_in.requests.ready = 1;
    chi_in.requester_responses.ready = 1;
    tick();
    send_request(32'h103f, 4'd6);
    for (int cycle = 0; cycle < 100 && requests == 0; cycle++) tick();
    assert (requests == 1 && responses == 0 && !core_out.drained)
      else $fatal(1, "RV32 zero completed without ownership");
    for (int packet = 0; packet < 4; packet++) begin
      for (int cycle = 0; cycle < 100 && !chi_out.response_data.ready; cycle++) tick();
      assert (chi_out.response_data.ready) else $fatal(1, "no refill data credit");
      chi_in.response_data.bits = '0;
      chi_in.response_data.bits.data = '1;
      chi_in.response_data.bits.byte_enable = '1;
      chi_in.response_data.bits.data_id = 2'(packet);
      chi_in.response_data.bits.resp = 3'b010;
      chi_in.response_data.bits.opcode = 4'h4;
      chi_in.response_data.bits.home_nid_or_pbha_or_mismatched_mecid = 7'd1;
      chi_in.response_data.bits.dbid_or_mecid = 16'h55;
      chi_in.response_data.bits.src_id = 7'd1;
      chi_in.response_data.bits.tgt_id = 7'd3;
      chi_in.response_data.valid = 1;
      tick();
      chi_in.response_data.valid = 0;
    end
    expect_response(32'd0);
    assert (acknowledgements == 1 && responses == 1) else $fatal(1, "RV32 zero completion count");
    for (int offset = 0; offset < 64; offset++) begin
      send_request(32'h1000 + 32'(offset), 4'd6);
      send_request(32'h1000 + 32'((offset / 4) * 4), 4'd1);
      expect_response(32'd0);
      expect_response(32'd0);
      for (int word = 0; word < 16; word++) begin
        send_request(32'h1000 + 32'(word * 4), 4'd1);
        expect_response(32'd0);
      end
      assert (requests == 1) else $fatal(1, "owned RV32 zero issued new traffic");
    end
    assert (core_out.drained) else $fatal(1, "RV32 zero failed to drain");
    // Colliding hinted misses must not evict the dirty zeroed line. Repeat
    // the same request with all selectors, signed/unsigned byte and half loads.
    expected_opcode = 7'h02;
    expected_address = 44'h2000;
    for (int hint = 1; hint <= 4; hint++) begin
      send_request(32'h203c, 4'd1, 3'(hint), hint <= 2 ? 2'd0 : 2'd1, !hint[0]);
      for (int cycle = 0; cycle < 100 && requests != hint + 1; cycle++) tick();
      assert (requests == hint + 1) else $fatal(1, "RV32 hinted miss unexpectedly allocated");
      for (int packet = 3; packet >= 0; packet--) begin
        for (int cycle = 0; cycle < 100 && !chi_out.response_data.ready; cycle++) tick();
        assert (chi_out.response_data.ready) else $fatal(1, "RV32 NTL refill timeout");
        chi_in.response_data.bits.data = '1;
        chi_in.response_data.bits.data_id = 2'(packet);
        chi_in.response_data.bits.resp = 3'b001;
        chi_in.response_data.valid = 1;
        tick();
        chi_in.response_data.valid = 0;
      end
      expect_response(hint[0] ? 32'hffff_ffff : hint == 2 ? 32'hff : 32'hffff);
      send_request(32'h103c, 4'd1, 3'(hint));
      expect_response(0);
      assert (requests == hint + 1 && acknowledgements == hint + 1)
        else $fatal(1, "RV32 NTL disturbed a dirty resident or lost CompAck");
    end
    // Populate the adjacent line without evicting the zeroed line at 0x1000.
    expected_opcode = 7'h07;
    expected_address = 44'h1040;
    send_request(32'h107f, 4'd6);
    for (int cycle = 0; cycle < 100 && requests != 6; cycle++) tick();
    assert (requests == 6) else $fatal(1, "adjacent line did not request ownership");
    for (int packet = 0; packet < 4; packet++) begin
      for (int cycle = 0; cycle < 100 && !chi_out.response_data.ready; cycle++) tick();
      assert (chi_out.response_data.ready) else $fatal(1, "adjacent refill timeout");
      chi_in.response_data.bits.data = '1;
      chi_in.response_data.bits.data_id = 2'(packet);
      chi_in.response_data.bits.resp = 3'b010;
      chi_in.response_data.valid = 1;
      tick();
      chi_in.response_data.valid = 0;
    end
    expect_response(0);
    for (int offset = 0; offset < 128; offset += 4) begin
      send_request(32'h1000 + 32'(offset), 4'd3);
      expect_response(0);
      assert (core_out.reservation_valid) else $fatal(1, "RV32 LR did not reserve word");
      send_request(32'h1000 + 32'(offset ^ 64), 4'd2);
      expect_response(0);
      assert (core_out.reservation_valid) else $fatal(1, "RV32 neighboring line cleared reservation");
      send_request(32'h1000 + 32'(offset), 4'd4, 0, 2, 0, 32'h1234);
      expect_response(0);
      assert (!core_out.reservation_valid) else $fatal(1, "RV32 SC retained reservation");
      send_request(32'h1000 + 32'(offset), 4'd4, 0, 2, 0, 32'h5678);
      expect_response(1);
      send_request(32'h1000 + 32'(offset), 4'd1);
      expect_response(32'h1234);
      send_request(32'h1000 + 32'(offset), 4'd3);
      expect_response(32'h1234);
      send_request(32'h1000 + 32'(offset ^ 4), 4'd4, 0, 2, 0, 32'h5678);
      expect_response(1);
      assert (!core_out.reservation_valid && requests == 6) else $fatal(1, "RV32 mismatched SC retained reservation or issued traffic");
      send_request(32'h1000 + 32'(offset ^ 4), 4'd1);
      expect_response(0);
      send_request(32'h1000 + 32'(offset), 4'd2);
      expect_response(0);
    end
    send_request(32'h103c, 4'd3);
    expect_response(0);
    // Same-line mutation is a permitted conservative reservation invalidation.
    send_request(32'h1000, 4'd2);
    expect_response(0);
    assert (!core_out.reservation_valid) else $fatal(1, "RV32 same-line store retained reservation");
    send_request(32'h103c, 4'd4, 0, 2, 0, 32'h5678);
    expect_response(1);
    send_request(32'h103c, 4'd3);
    expect_response(0);
    reset = 1;
    tick();
    assert (!core_out.reservation_valid) else $fatal(1, "RV32 reset retained reservation");
    $display("RV32 reservation bounds passed: 32 aligned W sites, adjacent-line isolation, exact address, one-shot SC");
    $display("RV32 block-zero SRAM sequencing passed");
    $finish;
  end
endmodule
