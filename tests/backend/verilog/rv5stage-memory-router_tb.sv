// Verifies PMA rejection, IO-MSHR admission, and cached/uncached data ordering.
module rv5stage_memory_router_tb;
  typedef struct packed {
    logic [31:0] address;
    logic [3:0] access;
    logic [3:0] atomic;
    logic [1:0] width;
    logic unsigned_0;
    logic [31:0] data;
    logic [1:0] destination;
    logic [4:0] rd;
    logic [1:0] floating_point_precision;
    logic [2:0] locality;
  } request_bits_t;
  typedef struct packed { logic valid; request_bits_t bits; } request_t;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed {
    logic access_fault;
    logic [31:0] data;
    logic [1:0] destination;
    logic [4:0] rd;
    logic [1:0] floating_point_precision;
  } response_bits_t;
  typedef struct packed { logic valid; response_bits_t bits; } response_t;
  typedef struct packed { request_t request; } requester_t;
  typedef struct packed { request_bits_t request; logic device; } uncached_request_bits_t;
  typedef struct packed { logic valid; uncached_request_bits_t bits; } uncached_request_t;
  typedef struct packed { uncached_request_t request; } uncached_requester_t;
  typedef struct packed {
    ready_t request;
    logic request_fault;
    logic request_access_fault;
    response_t response;
    logic drained;
  } responder_t;

  localparam logic [3:0] LOAD = 4'd1;
  localparam logic [3:0] STORE = 4'd2;
  localparam logic [3:0] LOAD_RESERVED = 4'd3;
  localparam logic [3:0] ATOMIC = 4'd5;

  logic clock = 1'b0;
  logic reset = 1'b1;
  requester_t core_in;
  typedef struct packed { ready_t request; logic request_fault; logic request_access_fault; response_t response; logic drained; logic reservation_valid; } cached_responder_t;
  cached_responder_t cache_in;
  responder_t uncached_in;
  cached_responder_t core_out;
  requester_t cache_out;
  uncached_requester_t uncached_out;

  RV5StageMemoryRouter dut (.*);

  task automatic tick;
    #5 clock = 1'b1;
    #1 clock = 1'b0;
    #4;
  endtask

  task automatic check_request(
      input logic [31:0] address,
      input logic [3:0] access,
      input logic expected_cache,
      input logic expected_device,
      input logic expected_access_fault
  );
    core_in.request.valid = 1'b1;
    core_in.request.bits.address = address;
    core_in.request.bits.access = access;
    core_in.request.bits.locality = 3'd4;
    #1;
    assert (core_out.request.ready &&
            cache_out.request.valid == expected_cache &&
            (!expected_cache || cache_out.request.bits.locality == 3'd4) &&
            !uncached_out.request.valid &&
            core_out.request_access_fault == expected_access_fault)
      else $fatal(1, "incorrect physical routing for address %h and access %0d", address, access);
    if (expected_device) begin
      tick();
      core_in.request.valid = 1'b0;
      #1;
      assert (uncached_out.request.valid && !core_out.drained &&
              uncached_out.request.bits.request.address == address &&
              uncached_out.request.bits.request.access == access &&
              uncached_out.request.bits.request.locality == 3'd4)
        else $fatal(1, "uncached request was not retained by the IO-MSHR");
      tick();
      uncached_in.response.valid = 1'b1;
      uncached_in.response.bits.data = 32'h12345678;
      #1;
      assert (core_out.response.valid && core_out.response.bits.data == 32'h12345678)
        else $fatal(1, "uncached completion payload was not forwarded");
      tick();
      uncached_in.response.valid = 1'b0;
    end
  endtask

  initial begin
    core_in = '0;
    cache_in = '0;
    uncached_in = '0;
    tick();
    reset = 1'b0;
    core_in.request.valid = 1'b1;
    cache_in.request.ready = 1'b1;
    cache_in.drained = 1'b1;
    uncached_in.request.ready = 1'b1;
    uncached_in.drained = 1'b1;
    cache_in.reservation_valid = 1'b1;
    #1;
    assert (core_out.reservation_valid) else $fatal(1, "reservation status was not routed");
    cache_in.reservation_valid = 1'b0;
    #1;
    assert (!core_out.reservation_valid) else $fatal(1, "reservation invalidation was not routed");

    check_request(32'h00001000, LOAD, 1'b1, 1'b0, 1'b0);
    check_request(32'h00001000, STORE, 1'b1, 1'b0, 1'b0);
    check_request(32'h00002000, LOAD, 1'b0, 1'b1, 1'b0);
    assert (uncached_out.request.bits.device)
      else $fatal(1, "device PMA was not forwarded to the uncached path");
    check_request(32'h00002000, ATOMIC, 1'b0, 1'b0, 1'b1);
    check_request(32'h00002000, 4'd0, 1'b0, 1'b0, 1'b1);
    check_request(32'h00003000, LOAD_RESERVED, 1'b0, 1'b0, 1'b1);
    check_request(32'h00003000, STORE, 1'b0, 1'b0, 1'b1);
    check_request(32'h00005000, LOAD, 1'b0, 1'b1, 1'b0);
    assert (!uncached_out.request.bits.device)
      else $fatal(1, "uncached normal memory was incorrectly marked as device memory");
    check_request(32'h00006000, LOAD, 1'b0, 1'b0, 1'b1);
    check_request(32'h00011000, LOAD, 1'b0, 1'b0, 1'b1);
    core_in.request.bits.width = 2'd3;
    check_request(32'h00004000, LOAD, 1'b0, 1'b0, 1'b1);
    core_in.request.bits.width = 2'd0;

    // Block permission is checked at both ends, independent of rs1 alignment
    // and scalar width. Uncached RAM is legal; devices and partial blocks are not.
    for (int offset = 0; offset < 64; offset++) begin
      check_request(32'h1000 + offset, 4'd6, 1'b1, 1'b0, 1'b0);
      check_request(32'h7000 + offset, 4'd6, 1'b0, 1'b1, 1'b0);
    end
    check_request(32'h2001, 4'd6, 1'b0, 1'b0, 1'b1);
    check_request(32'h3001, 4'd6, 1'b0, 1'b0, 1'b1);
    check_request(32'h5001, 4'd6, 1'b0, 1'b0, 1'b1);
    check_request(32'h8001, 4'd6, 1'b0, 1'b0, 1'b1);
    check_request(32'hffff, 4'd6, 1'b0, 1'b0, 1'b1);

    // Management is permitted by either read or write access, not CBZE/atomic
    // capability; static uncached regions complete without device accesses.
    for (int operation = 7; operation <= 9; operation++) begin
      check_request(32'h103f, 4'(operation), 1, 0, 0);
      check_request(32'h3001, 4'(operation), 1, 0, 0);
      check_request(32'h2001, 4'(operation), 0, 0, 0);
      check_request(32'h5001, 4'(operation), 0, 0, 0);
      check_request(32'h8001, 4'(operation), 0, 0, 1);
      check_request(32'h6001, 4'(operation), 0, 0, 1);
    end
    check_request(32'h5001, 4'd8, 0, 0, 0);
    clock = 1; #1; clock = 0;
    core_in.request.valid = 0;
    #1;
    assert (core_out.response.valid && !core_out.response.bits.access_fault && !cache_out.request.valid && !uncached_out.request.valid)
      else $fatal(1, "uncached maintenance failed its registered no-IO completion");
    clock = 1; #1; clock = 0;
    core_in.request.valid = 1;

    // Older cached work prevents IO admission, but instruction-owned RN-I
    // activity alone does not prevent a cached access or occupy the IO-MSHR.
    uncached_in.drained = 1'b0;
    uncached_in.request.ready = 1'b0;
    check_request(32'h00001000, LOAD, 1'b1, 1'b0, 1'b0);
    cache_in.drained = 1'b0;
    core_in.request.bits.address = 32'h2000;
    #1;
    assert (!core_out.request.ready && !uncached_out.request.valid)
      else $fatal(1, "IO admission passed older cached work");
    cache_in.drained = 1'b1;
    #1;
    assert (core_out.request.ready) else $fatal(1, "busy RN-I prevented IO admission");
    tick();
    core_in.request.bits.address = 32'h1000;
    repeat (4) begin
      #1;
      assert (!core_out.request.ready && !cache_out.request.valid &&
              !core_out.drained && uncached_out.request.valid &&
              uncached_out.request.bits.request.address == 32'h2000)
        else $fatal(1, "queued IO did not retain payload or block younger cached work");
      core_in.request.bits.address = 32'h5001;
      core_in.request.bits.access = 4'd8;
      #1;
      assert (!core_out.request.ready && !core_out.response.valid)
        else $fatal(1, "uncached CMO bypassed an older IO-MSHR operation");
      core_in.request.bits.address = 32'h1000;
      core_in.request.bits.access = LOAD;
      tick();
    end
    uncached_in.request.ready = 1'b1;
    tick();
    repeat (4) begin
      assert (!uncached_out.request.valid && !core_out.request.ready && !core_out.drained)
        else $fatal(1, "IO slot released at issue rather than completion");
      tick();
    end
    uncached_in.response.valid = 1'b1;
    tick();
    uncached_in.response.valid = 1'b0;
    check_request(32'h00001000, LOAD, 1'b1, 1'b0, 1'b0);

    cache_in.request_access_fault = 1'b1;
    check_request(32'h00001000, LOAD, 1'b1, 1'b0, 1'b1);
    cache_in.request_access_fault = 1'b0;
    cache_in.drained = 1'b0;
    uncached_in.drained = 1'b0;
    check_request(32'h00006000, LOAD, 1'b0, 1'b0, 1'b1);

    core_in.request.valid = 1'b0;
    cache_in.response.valid = 1'b1;
    cache_in.response.bits = '{access_fault: 1'b0, data: 32'habcdef01,
                               destination: 2'd1, rd: 5'd7, floating_point_precision: 2'd2};
    #1;
    assert (core_out.response == cache_in.response)
      else $fatal(1, "cached completion payload was not forwarded");
    cache_in.response.valid = 1'b0;
    #1;
    assert (!core_out.request_access_fault)
      else $fatal(1, "idle physical router reported an access fault");
    $display("RV5Stage physical memory routing passed");
    $finish;
  end
endmodule
