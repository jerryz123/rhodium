// Verifies RV5Stage instruction/data arbitration, non-allocating fetches, and Home-routed device stores.
module rv5stage_uncached_tb;
  typedef struct packed {
    logic flush;
    logic invalidate_all;
    struct packed { logic valid; RV5StagePhysicalInstructionReq bits; } request;
    struct packed { logic ready; } response;
  } instruction_in_t;
  typedef struct packed {
    struct packed { logic valid; RV5StageUncachedDataReq bits; } request;
  } core_in_t;
  typedef struct packed {
    struct packed { logic ready; } req;
    struct packed {
      struct packed { logic ready; } requester;
      struct packed { logic valid; CHIRspFlit bits; } response;
    } rsp;
    struct packed {
      struct packed { logic ready; } request;
      struct packed { logic valid; CHIDatFlit bits; } response;
    } dat;
  } chi_in_t;
  typedef struct packed {
    struct packed { logic ready; } request;
    struct packed { logic valid; RV5StageInstructionResp bits; } response;
  } instruction_out_t;
  typedef struct packed {
    struct packed { logic ready; } request;
    logic request_fault;
    logic request_access_fault;
    struct packed { logic valid; RV5StageDataResp bits; } response;
    logic drained;
  } core_out_t;
  typedef struct packed {
    struct packed { logic valid; CHIReqFlit bits; } req;
    struct packed {
      struct packed { logic valid; CHIRspFlit bits; } requester;
      struct packed { logic ready; } response;
    } rsp;
    struct packed {
      struct packed { logic valid; CHIDatFlit bits; } request;
      struct packed { logic ready; } response;
    } dat;
  } chi_out_t;

  localparam logic [2:0] LOAD = 3'd1;
  localparam logic [6:0] READ_NO_SNP = 7'h04;
  localparam logic [3:0] COMP_DATA = 4'h4;

  logic clock = 1'b0;
  logic reset = 1'b1;
  logic [6:0] node_id = 7'd5;
  instruction_in_t instruction_in;
  core_in_t core_in;
  chi_in_t chi_in;
  instruction_out_t instruction_out;
  core_out_t core_out;
  chi_out_t chi_out;

  RV5StageUncached dut (.*);

  task automatic tick;
    #5 clock = 1'b1;
    #1 clock = 1'b0;
    #4;
  endtask

  task automatic present_read_data(input logic [127:0] data);
    chi_in.dat.response.bits = '0;
    chi_in.dat.response.bits.opcode = COMP_DATA;
    chi_in.dat.response.bits.home_nid_or_pbha_or_mismatched_mecid = 7'd4;
    chi_in.dat.response.bits.data = data;
    chi_in.dat.response.valid = 1'b1;
  endtask

  initial begin
    instruction_in = '0;
    core_in = '0;
    chi_in = '0;
    tick();
    reset = 1'b0;
    chi_in.req.ready = 1'b1;
    chi_in.dat.request.ready = 1'b1;

    // A presented data load wins arbitration over a simultaneous instruction
    // fetch and retains ownership of the eventual response.
    instruction_in.request.valid = 1'b1;
    instruction_in.request.bits.address = 64'h0000_c00c;
    instruction_in.request.bits.cacheable = 1'b0;
    instruction_in.request.bits.device = 1'b0;
    instruction_in.response.ready = 1'b0;
    core_in.request.valid = 1'b1;
    core_in.request.bits.request.address = 64'h0000_8000;
    core_in.request.bits.request.access = LOAD;
    core_in.request.bits.request.width = 2'd3;
    core_in.request.bits.device = 1'b1;
    #1;
    assert (core_out.request.ready && !instruction_out.request.ready && !chi_out.req.valid)
      else $fatal(1, "uncached data request did not win arbitration");
    tick();
    core_in.request.valid = 1'b0;
    #1;
    assert (chi_out.req.valid && chi_out.req.bits.address == 44'h8000 &&
            chi_out.req.bits.opcode == READ_NO_SNP &&
            chi_out.req.bits.size_or_num_req == 6'd3 &&
            chi_out.req.bits.mem_attr.device &&
            !chi_out.req.bits.mem_attr.cacheable &&
            !chi_out.req.bits.mem_attr.allocate)
      else $fatal(1, "uncached data request used incorrect CHI attributes");
    tick();
    #1;
    assert (!chi_out.req.valid && !instruction_out.request.ready)
      else $fatal(1, "instruction request bypassed the outstanding data read");

    present_read_data(128'h0000_0000_0000_0000_0123_4567_89ab_cdef);
    #1;
    assert (core_out.response.valid && core_out.response.bits.data == 64'h0123_4567_89ab_cdef &&
            !instruction_out.response.valid)
      else $fatal(1, "uncached data response reached the wrong owner");
    tick();
    chi_in.dat.response.valid = 1'b0;

    // The waiting fetch now emits one aligned four-byte ReadNoSnp. The
    // non-device, non-cacheable attributes match a BootROM PMA.
    #1;
    assert (instruction_out.request.ready && !chi_out.req.valid)
      else $fatal(1, "waiting instruction request was not selected");
    tick();
    instruction_in.request.valid = 1'b0;
    #1;
    assert (chi_out.req.valid &&
            chi_out.req.bits.address == 44'hc00c &&
            chi_out.req.bits.tgt_id == 7'd4 &&
            chi_out.req.bits.opcode == READ_NO_SNP &&
            chi_out.req.bits.size_or_num_req == 6'd2 &&
            !chi_out.req.bits.mem_attr.device &&
            !chi_out.req.bits.mem_attr.cacheable &&
            !chi_out.req.bits.mem_attr.allocate)
      else $fatal(1, "uncached instruction request used incorrect CHI encoding");
    tick();
    present_read_data(128'h4433_2211_cccc_dddd_aaaa_bbbb_1234_5678);
    #1;
    assert (instruction_out.response.valid &&
            instruction_out.response.bits.word == 32'h4433_2211 &&
            !chi_out.dat.response.ready && !core_out.response.valid)
      else $fatal(1, "uncached instruction response or backpressure was incorrect");
    instruction_in.response.ready = 1'b1;
    #1;
    assert (chi_out.dat.response.ready)
      else $fatal(1, "instruction response readiness did not reach CHI");
    tick();
    chi_in.dat.response.valid = 1'b0;

    // A flushed fetch still drains its already-issued CHI transaction but no
    // longer publishes the returned instruction word.
    instruction_in.request.valid = 1'b1;
    instruction_in.request.bits.address = 64'h0000_c000;
    #1;
    assert (instruction_out.request.ready && !chi_out.req.valid)
      else $fatal(1, "second uncached instruction request was not accepted");
    tick();
    instruction_in.request.valid = 1'b0;
    #1;
    assert (chi_out.req.valid)
      else $fatal(1, "captured uncached instruction request was not issued");
    tick();
    instruction_in.flush = 1'b1;
    tick();
    instruction_in.flush = 1'b0;
    present_read_data(128'hfeed_face_dead_beef_0123_4567_89ab_cdef);
    #1;
    assert (chi_out.dat.response.ready && !instruction_out.response.valid)
      else $fatal(1, "flushed uncached instruction response was not silently drained");
    tick();
    chi_in.dat.response.valid = 1'b0;
    #1;
    assert (core_out.drained)
      else $fatal(1, "uncached engine did not return to idle");

    // One accepted, arbitrarily aligned zero request stays outstanding across
    // eight acknowledged writes, including backpressure at every CHI boundary.
    core_in.request.valid = 1'b1;
    core_in.request.bits.request.address = 64'hc03f;
    core_in.request.bits.request.access = 3'd6;
    core_in.request.bits.request.destination = 2'd0;
    core_in.request.bits.device = 1'b0;
    tick();
    core_in.request.valid = 1'b0;
    for (int word = 0; word < 8; word++) begin
      chi_in.req.ready = 1'b0;
      repeat (3) begin
        #1;
        assert (chi_out.req.valid && chi_out.req.bits.address == 44'hc000 + 44'(word * 8) &&
                chi_out.req.bits.opcode == 7'h1c && chi_out.req.bits.size_or_num_req == 6'd3 &&
                !chi_out.req.bits.mem_attr.device && !chi_out.req.bits.mem_attr.cacheable &&
                !core_out.drained && !core_out.response.valid && !core_out.request.ready)
          else $fatal(1, "incorrect zero write request %0d", word);
        tick();
      end
      chi_in.req.ready = 1'b1;
      tick();
      chi_in.rsp.response.bits = '0;
      chi_in.rsp.response.bits.opcode = 5'h06;
      chi_in.rsp.response.bits.src_id = 7'd4;
      chi_in.rsp.response.bits.dbid_or_group_id = 12'h33;
      chi_in.rsp.response.valid = 1'b1;
      #1;
      assert (chi_out.rsp.response.ready) else $fatal(1, "zero DBID not accepted");
      tick();
      chi_in.rsp.response.valid = 1'b0;
      chi_in.dat.request.ready = 1'b0;
      repeat (3) begin
        #1;
        assert (chi_out.dat.request.valid && chi_out.dat.request.bits.data == 0 &&
                chi_out.dat.request.bits.byte_enable == (((word & 1) != 0) ? 16'hff00 : 16'h00ff) &&
                chi_out.dat.request.bits.txn_id == 12'h33 && !core_out.response.valid && !core_out.drained)
          else $fatal(1, "incorrect zero write data %0d", word);
        tick();
      end
      chi_in.dat.request.ready = 1'b1;
      tick();
      repeat (3) begin
        assert (!core_out.response.valid && !core_out.drained)
          else $fatal(1, "zero completed before acknowledgement");
        tick();
      end
      chi_in.rsp.response.bits.opcode = 5'h04;
      chi_in.rsp.response.valid = 1'b1;
      #1;
      assert (core_out.response.valid == (word == 7) && !core_out.drained)
        else $fatal(1, "zero produced an intermediate completion");
      tick();
      chi_in.rsp.response.valid = 1'b0;
    end
    #1;
    assert (core_out.drained && !core_out.response.valid && !chi_out.req.valid)
      else $fatal(1, "zero did not complete exactly once");

    // Device writes must use Home-issued DBIDs, not direct write transfer.
    core_in.request.valid = 1'b1;
    core_in.request.bits.request.address = 64'h8004;
    core_in.request.bits.request.access = 3'd2; // Store
    core_in.request.bits.request.width = 2'd2;
    core_in.request.bits.request.data = 64'h12345678;
    core_in.request.bits.device = 1'b1;
    #1;
    assert (core_out.request.ready) else $fatal(1, "device store not accepted");
    tick();
    core_in.request.valid = 0;
    #1;
    assert (chi_out.req.valid && chi_out.req.bits.opcode == 7'h1c &&
            !chi_out.req.bits.snp_attr_or_do_dwt)
      else $fatal(1, "device store incorrectly requested direct write transfer");
    tick();
    chi_in.rsp.response.valid = 1;
    chi_in.rsp.response.bits = '0;
    chi_in.rsp.response.bits.opcode = 5'h06; // DBIDResp
    chi_in.rsp.response.bits.src_id = 7'd4;
    chi_in.rsp.response.bits.dbid_or_group_id = 12'h123;
    tick();
    chi_in.rsp.response.valid = 0;
    #1;
    assert (chi_out.dat.request.valid && chi_out.dat.request.bits.tgt_id == 7'd4 &&
            chi_out.dat.request.bits.txn_id == 12'h123 &&
            chi_out.dat.request.bits.byte_enable == 16'h00f0 &&
            chi_out.dat.request.bits.data == 128'h1234567800000000)
      else $fatal(1, "device store did not use the Home DBID and correct byte lanes");
    tick();
    chi_in.rsp.response.valid = 1;
    chi_in.rsp.response.bits.opcode = 5'h04; // Comp
    #1;
    assert (core_out.response.valid) else $fatal(1, "device store did not complete");
    tick();
    chi_in.rsp.response.valid = 0;
    #1;
    assert (core_out.drained) else $fatal(1, "device store did not drain");

    $display("RV5Stage shared uncached instruction/data path passed");
    $finish;
  end
endmodule
