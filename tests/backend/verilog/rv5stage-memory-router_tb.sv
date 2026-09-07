// Verifies that RV5Stage rejects inaccessible physical requests before either memory path.
module rv5stage_memory_router_tb;
  typedef struct packed {
    logic [31:0] address;
    logic [2:0] access;
    logic [3:0] atomic;
    logic [1:0] width;
    logic unsigned_0;
    logic [31:0] data;
    logic [1:0] destination;
    logic [4:0] rd;
    logic [1:0] floating_point_precision;
  } request_bits_t;
  typedef struct packed { logic valid; request_bits_t bits; } request_t;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed {
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

  localparam logic [2:0] LOAD = 3'd1;
  localparam logic [2:0] STORE = 3'd2;
  localparam logic [2:0] LOAD_RESERVED = 3'd3;
  localparam logic [2:0] ATOMIC = 3'd5;

  logic clock = 1'b0;
  logic reset = 1'b0;
  requester_t core_in;
  responder_t cache_in;
  responder_t uncached_in;
  responder_t core_out;
  requester_t cache_out;
  uncached_requester_t uncached_out;

  RV5StageMemoryRouter dut (.*);

  task automatic check_request(
      input logic [31:0] address,
      input logic [2:0] access,
      input logic expected_cache,
      input logic expected_device,
      input logic expected_access_fault
  );
    core_in.request.bits.address = address;
    core_in.request.bits.access = access;
    #1;
    assert (core_out.request.ready &&
            cache_out.request.valid == expected_cache &&
            uncached_out.request.valid == expected_device &&
            core_out.request_access_fault == expected_access_fault)
      else $fatal(1, "incorrect physical routing for address %h and access %0d", address, access);
  endtask

  initial begin
    core_in = '0;
    cache_in = '0;
    uncached_in = '0;
    core_in.request.valid = 1'b1;
    cache_in.request.ready = 1'b1;
    cache_in.drained = 1'b1;
    uncached_in.request.ready = 1'b1;
    uncached_in.drained = 1'b1;

    check_request(32'h00001000, LOAD, 1'b1, 1'b0, 1'b0);
    check_request(32'h00001000, STORE, 1'b1, 1'b0, 1'b0);
    check_request(32'h00002000, LOAD, 1'b0, 1'b1, 1'b0);
    assert (uncached_out.request.bits.device)
      else $fatal(1, "device PMA was not forwarded to the uncached path");
    check_request(32'h00002000, ATOMIC, 1'b0, 1'b0, 1'b1);
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
      check_request(32'h1000 + offset, 3'd6, 1'b1, 1'b0, 1'b0);
      check_request(32'h7000 + offset, 3'd6, 1'b0, 1'b1, 1'b0);
    end
    check_request(32'h2001, 3'd6, 1'b0, 1'b0, 1'b1);
    check_request(32'h3001, 3'd6, 1'b0, 1'b0, 1'b1);
    check_request(32'h5001, 3'd6, 1'b0, 1'b0, 1'b1);
    check_request(32'h8001, 3'd6, 1'b0, 1'b0, 1'b1);
    check_request(32'hffff, 3'd6, 1'b0, 1'b0, 1'b1);

    cache_in.request_access_fault = 1'b1;
    check_request(32'h00001000, LOAD, 1'b1, 1'b0, 1'b1);
    cache_in.request_access_fault = 1'b0;
    cache_in.drained = 1'b0;
    uncached_in.drained = 1'b0;
    check_request(32'h00006000, LOAD, 1'b0, 1'b0, 1'b1);

    core_in.request.valid = 1'b0;
    #1;
    assert (!core_out.request_access_fault)
      else $fatal(1, "idle physical router reported an access fault");
    $display("RV5Stage physical memory routing passed");
    $finish;
  end
endmodule
