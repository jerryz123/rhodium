// Verifies instruction routing, owner capacity, request/response stalls, and flush cancellation.
module rv5stage_instruction_memory_router_tb;
  typedef struct packed {
    logic flush;
    logic invalidate_all;
    struct packed { logic valid; RV5StagePhysicalInstructionReq bits; } request;
    struct packed { logic ready; } response;
  } core_in_t;
  typedef struct packed {
    struct packed { logic ready; } request;
    struct packed { logic valid; RV5StageInstructionResp bits; } response;
  } memory_in_t;
  typedef struct packed {
    struct packed { logic ready; } request;
    struct packed { logic valid; RV5StageInstructionResp bits; } response;
  } core_out_t;
  typedef struct packed {
    logic flush;
    logic invalidate_all;
    struct packed { logic valid; RV5StageInstructionReq bits; } request;
    struct packed { logic ready; } response;
  } cache_out_t;
  typedef struct packed {
    logic flush;
    logic invalidate_all;
    struct packed { logic valid; RV5StagePhysicalInstructionReq bits; } request;
    struct packed { logic ready; } response;
  } uncached_out_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  core_in_t core_in;
  memory_in_t cache_in;
  memory_in_t uncached_in;
  core_out_t core_out;
  cache_out_t cache_out;
  uncached_out_t uncached_out;

  RV5StageInstructionMemoryRouter dut (.*);

  task automatic tick;
    #5 clock = 1'b1;
    #1 clock = 1'b0;
    #4;
  endtask

  initial begin
    core_in = '0;
    cache_in = '0;
    uncached_in = '0;
    tick();
    reset = 1'b0;
    cache_in.request.ready = 1'b1;
    uncached_in.request.ready = 1'b1;
    core_in.response.ready = 1'b1;

    core_in.request.valid = 1'b1;
    core_in.request.bits.address = 64'h1000;
    core_in.request.bits.cacheable = 1'b1;
    cache_in.request.ready = 1'b0;
    repeat (3) begin
      #1;
      assert (!core_out.request.ready && cache_out.request.valid && !uncached_out.request.valid &&
              cache_out.request.bits.address == 64'h1000)
        else $fatal(1, "cached request did not retain routing under backpressure");
      tick();
    end
    cache_in.request.ready = 1'b1;
    #1;
    assert (core_out.request.ready && cache_out.request.valid &&
            !uncached_out.request.valid && cache_out.request.bits.address == 64'h1000)
      else $fatal(1, "cacheable instruction request did not route to L1I");
    tick();

    core_in.request.bits.address = 64'hc000;
    core_in.request.bits.cacheable = 1'b0;
    core_in.request.bits.device = 1'b0;
    #1;
    assert (core_out.request.ready && !cache_out.request.valid &&
            uncached_out.request.valid &&
            uncached_out.request.bits.address == 64'hc000 &&
            !uncached_out.request.bits.cacheable && !uncached_out.request.bits.device)
      else $fatal(1, "non-cacheable instruction request did not route to RN-I");
    tick();

    // Both owner slots are occupied; a third fetch cannot reach either path.
    core_in.request.bits.address = 64'hd000;
    #1;
    assert (!core_out.request.ready && !cache_out.request.valid && !uncached_out.request.valid)
      else $fatal(1, "request escaped without a free response-owner slot");
    tick();
    core_in.request.valid = 1'b0;

    // The second response cannot pass the first even if it arrives first.
    uncached_in.response.valid = 1'b1;
    uncached_in.response.bits.word = 32'h2222_2222;
    #1;
    assert (!core_out.response.valid && !uncached_out.response.ready)
      else $fatal(1, "uncached response bypassed an older cached response");
    cache_in.response.valid = 1'b1;
    cache_in.response.bits.word = 32'h1111_1111;
    core_in.response.ready = 1'b0;
    repeat (3) begin
      #1;
      assert (core_out.response.valid && core_out.response.bits.word == 32'h1111_1111 &&
              !cache_out.response.ready && !uncached_out.response.ready)
        else $fatal(1, "stalled response lost its owner or advanced another path");
      tick();
    end
    core_in.response.ready = 1'b1;
    #1;
    assert (core_out.response.valid && core_out.response.bits.word == 32'h1111_1111 &&
            cache_out.response.ready && !uncached_out.response.ready)
      else $fatal(1, "oldest cached response was not selected");
    tick();
    cache_in.response.valid = 1'b0;
    #1;
    assert (core_out.response.valid && core_out.response.bits.word == 32'h2222_2222 &&
            uncached_out.response.ready)
      else $fatal(1, "uncached response was not selected after its predecessor");
    tick();
    uncached_in.response.valid = 1'b0;

    core_in.request.valid = 1'b1;
    core_in.request.bits.cacheable = 1'b1;
    tick();
    cache_in.response.valid = 1'b1;
    core_in.flush = 1'b1;
    core_in.request.valid = 1'b1;
    #1;
    assert (!core_out.request.ready && !cache_out.request.valid &&
            !uncached_out.request.valid && !core_out.response.valid &&
            !cache_out.response.ready && !uncached_out.response.ready && cache_out.flush && uncached_out.flush)
      else $fatal(1, "instruction flush did not suppress and propagate correctly");
    tick();
    core_in.flush = 1'b0;
    core_in.request.valid = 1'b0;
    cache_in.response.valid = 1'b0;
    #1;
    assert (!core_out.response.valid)
      else $fatal(1, "flushed response owner survived cancellation");

    $display("RV5Stage instruction memory routing passed");
    $finish;
  end
endmodule
