// Verifies IO-MSHR retention, data ordering, exactly-once issue, and fetch-flush independence.
module rv5stage_io_mshr_tb;
  typedef struct packed {
    logic flush;
    logic invalidate_all;
    struct packed { logic valid; RV5StagePhysicalInstructionReq bits; } request;
    struct packed { logic ready; } response;
  } instruction_in_t;
  typedef struct packed {
    struct packed { logic ready; } request;
    struct packed { logic valid; RV5StageInstructionResp bits; } response;
  } instruction_out_t;
  typedef struct packed {
    struct packed { logic valid; RV5StageDataReq bits; } request;
  } data_requester_t;
  typedef struct packed {
    struct packed { logic ready; } request;
    logic request_fault;
    logic request_access_fault;
    struct packed { logic valid; RV5StageDataResp bits; } response;
    logic drained; logic reservation_valid;
  } data_responder_t;
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

  logic clock = 0;
  logic reset = 1;
  logic [6:0] node_id = 7'd5;
  instruction_in_t instruction_in;
  instruction_out_t instruction_out;
  data_requester_t core_in, cache_out;
  data_responder_t core_out, cache_in;
  chi_in_t chi_in;
  chi_out_t chi_out;
  int accepted = 0, completed = 0, requests = 0, writes = 0, outstanding = 0;

  RV5StageIOMSHRFixture dut (.*);

  always @(posedge clock) begin
    if (reset) begin
      accepted = 0;
      completed = 0;
      requests = 0;
      writes = 0;
      outstanding = 0;
    end else begin
      if (core_in.request.valid && core_out.request.ready && !core_out.request_access_fault)
        accepted++;
      if (core_out.response.valid) completed++;
      if (chi_out.req.valid && chi_in.req.ready) begin
        requests++;
        outstanding++;
      end
      if (chi_out.dat.request.valid && chi_in.dat.request.ready) writes++;
      if (chi_in.dat.response.valid && chi_out.dat.response.ready) outstanding--;
      if (chi_in.rsp.response.valid && chi_out.rsp.response.ready &&
          chi_in.rsp.response.bits.opcode == 5'h04) outstanding--;
      assert (outstanding >= 0 && outstanding <= 1 && completed <= accepted)
        else $fatal(1, "duplicate issue/completion or more than one CHI transaction");
    end
  end

  task automatic tick;
    #5 clock = 1;
    #1 clock = 0;
    #4;
  endtask

  task automatic read_data(input logic [127:0] data);
    chi_in.dat.response.valid = 1;
    chi_in.dat.response.bits = '0;
    chi_in.dat.response.bits.opcode = 4'h4;
    chi_in.dat.response.bits.home_nid_or_pbha_or_mismatched_mecid = 7'd4;
    chi_in.dat.response.bits.data = data;
  endtask

  task automatic fetch;
    instruction_in.request.valid = 1;
    instruction_in.request.bits.address = 64'hc00c;
    #1;
    assert (instruction_out.request.ready) else $fatal(1, "fetch not accepted");
    tick();
    instruction_in.request.valid = 0;
  endtask

  task automatic contended_load(input bit flush_fetch);
    int initial_requests, initial_accepted, initial_completed;
    initial_requests = requests;
    initial_accepted = accepted;
    initial_completed = completed;
    fetch();
    tick(); // Fetch reaches CHI; its response is delayed.
    core_in.request.valid = 1;
    core_in.request.bits = '0;
    core_in.request.bits.address = 64'h8004;
    core_in.request.bits.access = 4'd1;
    core_in.request.bits.width = 2'd2;
    core_in.request.bits.unsigned_0 = 1;
    core_in.request.bits.destination = 2'd2;
    core_in.request.bits.rd = 5'd17;
    core_in.request.bits.floating_point_precision = 2'd1;
    #1;
    assert (core_out.request.ready && !core_out.request_access_fault)
      else $fatal(1, "instruction-owned RN-I blocked data admission");
    assert (!core_out.drained) else $fatal(1, "same-cycle IO admission reported quiescence");
    tick();
    // Live WB inputs may change after acceptance. Neither another IO request
    // nor a cached request may replace or pass the retained operation.
    core_in.request.bits = '0;
    core_in.request.bits.address = 64'h8010;
    core_in.request.bits.access = 4'd2;
    repeat (4) begin
      #1;
      assert (!core_out.request.ready && !core_out.drained && !chi_out.req.valid)
        else $fatal(1, "IO-MSHR failed to hold a queued data operation");
      tick();
    end
    core_in.request.bits.address = 64'h1000;
    instruction_in.flush = flush_fetch;
    tick();
    instruction_in.flush = 0;
    instruction_in.response.ready = 0;
    read_data(128'h00008067_00000000_00000000_00000000); // jalr fetch response
    #1;
    assert (!core_out.response.valid && !core_out.request.ready && !cache_out.request.valid &&
            instruction_out.response.valid == !flush_fetch &&
            chi_out.dat.response.ready == flush_fetch)
      else $fatal(1, "fetch response escaped flush/backpressure or released data slot");
    if (!flush_fetch) begin
      repeat (4) tick();
      instruction_in.response.ready = 1;
    end
    tick();
    chi_in.dat.response.valid = 0;
    // A new fetch is eligible, but the retained data operation must win.
    instruction_in.request.valid = 1;
    #1;
    assert (!instruction_out.request.ready && !core_out.request.ready)
      else $fatal(1, "new fetch bypassed retained data request");
    tick();
    chi_in.req.ready = 0;
    repeat (4) begin
      #1;
      assert (chi_out.req.valid && chi_out.req.bits.address == 44'h8004 &&
              chi_out.req.bits.opcode == 7'h04 && chi_out.req.bits.size_or_num_req == 6'd2 &&
              chi_out.req.bits.mem_attr.device && !chi_out.req.bits.mem_attr.cacheable &&
              !chi_out.req.bits.mem_attr.allocate && !core_out.drained)
        else $fatal(1, "retained load changed under backpressure");
      tick();
    end
    chi_in.req.ready = 1;
    tick();
    instruction_in.request.valid = 0;
    core_in.request.valid = 0;
    // A data-owned transaction is not canceled by a subsequent fetch flush.
    instruction_in.flush = 1;
    tick();
    instruction_in.flush = 0;
    read_data(128'h0000000000000000_8000000000000000);
    #1;
    assert (core_out.response.valid && core_out.response.bits.data == 64'h80000000 &&
            core_out.response.bits.destination == 2'd2 && core_out.response.bits.rd == 5'd17 &&
            core_out.response.bits.floating_point_precision == 2'd1 && !core_out.drained)
      else $fatal(1, "load completion lost retained width, signedness, or destination metadata");
    tick();
    chi_in.dat.response.valid = 0;
    repeat (4) tick();
    assert (core_out.drained && requests == initial_requests + 2 &&
            accepted == initial_accepted + 1 && completed == initial_completed + 1)
      else $fatal(1, "contended load did not complete exactly once");
  endtask

  initial begin
    core_in = '0;
    cache_in = '0;
    instruction_in = '0;
    chi_in = '0;
    tick();
    reset = 0;
    cache_in.request.ready = 1;
    cache_in.drained = 1;
    chi_in.req.ready = 1;
    chi_in.dat.request.ready = 1;

    contended_load(0);
    contended_load(1);

    // Queue a store behind an unissued fetch, then cancel only the fetch.
    chi_in.req.ready = 0;
    fetch();
    core_in.request.valid = 1;
    core_in.request.bits = '0;
    core_in.request.bits.address = 64'h8004;
    core_in.request.bits.access = 4'd2;
    core_in.request.bits.width = 2'd2;
    core_in.request.bits.data = 64'h12345678;
    #1;
    assert (core_out.request.ready) else $fatal(1, "store slot not available behind fetch");
    tick();
    core_in.request = '0;
    instruction_in.flush = 1;
    tick();
    instruction_in.flush = 0;
    tick();
    repeat (4) begin
      assert (chi_out.req.valid && chi_out.req.bits.address == 44'h8004 &&
              chi_out.req.bits.opcode == 7'h1c && !core_out.drained)
        else $fatal(1, "fetch cancellation discarded queued store");
      tick();
    end
    chi_in.req.ready = 1;
    tick();
    chi_in.rsp.response.valid = 1;
    chi_in.rsp.response.bits = '0;
    chi_in.rsp.response.bits.opcode = 5'h06;
    chi_in.rsp.response.bits.src_id = 7'd4;
    chi_in.rsp.response.bits.dbid_or_group_id = 12'h123;
    tick();
    chi_in.rsp.response.valid = 0;
    chi_in.dat.request.ready = 0;
    repeat (4) begin
      assert (chi_out.dat.request.valid && chi_out.dat.request.bits.txn_id == 12'h123 &&
              chi_out.dat.request.bits.byte_enable == 16'h00f0 &&
              chi_out.dat.request.bits.data == 128'h1234567800000000 && !core_out.drained)
        else $fatal(1, "store data, mask, or DBID changed while stalled");
      tick();
    end
    chi_in.dat.request.ready = 1;
    tick();
    repeat (6) begin
      assert (!core_out.drained && !core_out.response.valid && !core_out.request.ready)
        else $fatal(1, "store slot released before final acknowledgement");
      tick();
    end
    chi_in.rsp.response.bits.opcode = 5'h04;
    chi_in.rsp.response.valid = 1;
    #1;
    assert (core_out.response.valid) else $fatal(1, "store did not complete");
    tick();
    chi_in.rsp.response.valid = 0;
    repeat (4) tick();
    assert (core_out.drained && accepted == 3 && completed == 3 && requests == 5 && writes == 1)
      else $fatal(1, "unexpected admission, completion, or CHI transfer count");

    // Reset clears a queued slot along with the shared transaction engine.
    fetch();
    tick();
    core_in.request.valid = 1;
    core_in.request.bits.address = 64'h8000;
    core_in.request.bits.access = 4'd1;
    tick();
    reset = 1;
    core_in = '0;
    tick();
    reset = 0;
    repeat (4) tick();
    assert (core_out.drained && core_out.request.ready && !core_out.response.valid && !chi_out.req.valid)
      else $fatal(1, "reset leaked a retained IO operation");
    $display("RV5Stage IO-MSHR contention and completion ownership passed");
    $finish;
  end
endmodule
