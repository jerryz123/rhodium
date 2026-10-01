// SPDX-License-Identifier: Apache-2.0
module spike_attributes_tb;
  typedef struct packed {
    logic [63:0] address;
    logic [2:0] size;
    logic write_0, execute;
  } request_t;
  typedef struct packed {
    logic grant_valid;
    logic [63:0] grant_base, grant_limit;
    logic readable, writable, executable;
    logic cacheable, cache_block_zero, instruction_cacheable, device, atomic_0, access_fault;
  } response_t;
  request_t request;
  response_t response, narrow;
  SpikeAttributeFixture dut (.*);

  task automatic check_access(input logic [63:0] address, input int size,
                             input logic mapped, input logic grant,
                             input logic [63:0] base, limit,
                             input logic writable, executable, cached, device);
    for (int mode = 0; mode < 3; mode++) begin
      request = {address, 3'(size), mode == 1, mode == 2};
      #1;
      if (response.grant_valid !== grant || response.access_fault !==
          (!mapped || (mode == 1 && !writable) || (mode == 2 && !executable)))
        $fatal(1, "classification address=%h size=%0d mode=%0d: %h", address, size, mode, response);
      if (grant && (response.grant_base !== base || response.grant_limit !== limit))
        $fatal(1, "incorrect certified interval at %h", address);
      if (mapped && (response.readable !== 1 || response.writable !== writable ||
          response.executable !== executable || response.cacheable !== cached ||
          response.cache_block_zero !== cached || response.instruction_cacheable !== cached ||
          response.atomic_0 !== cached || response.device !== device))
        $fatal(1, "incorrect attributes at %h", address);
      if (address < 64'h10000 && response !== narrow)
        $fatal(1, "narrow map changed a fitting access");
      if (address >= 64'h10000 && (narrow.grant_valid || !narrow.access_fault))
        $fatal(1, "narrow map accepted out-of-width address");
    end
  endtask

  initial begin
    for (int size = 0; size <= 6; size++) begin
      check_access(64'h1000, size, 1, 1, 64'h1000, 64'h10ff, 1, 1, 1, 0);
      check_access(64'h2000, size, 1, 1, 64'h2000, 64'h20ff, 0, 0, 0, 0);
    end
    check_access(64'h10ff, 0, 1, 1, 64'h1000, 64'h10ff, 1, 1, 1, 0);
    check_access(64'h10ff, 1, 0, 0, 0, 0, 0, 0, 0, 0);
    check_access(64'h3000, 3, 1, 1, 64'h3000, 64'h3007, 1, 0, 0, 1);
    check_access(64'h3004, 3, 0, 0, 0, 0, 0, 0, 0, 0);
    check_access(64'h4000, 2, 1, 1, 64'h4000, 64'h4003, 1, 1, 1, 0);
    check_access(64'h4008, 2, 1, 1, 64'h4008, 64'h400b, 1, 1, 1, 0);
    check_access(64'h4004, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    // Preserve the current lookup's result, but never grant a range spanning a hole.
    check_access(64'h4002, 3, 1, 0, 0, 0, 1, 1, 1, 0);
    check_access(64'hffff, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    check_access(64'hfffffffffffffff8, 3, 1, 1, 64'hfffffffffffffff8, 64'hffffffffffffffff, 1, 1, 1, 0);
    check_access(64'hffffffffffffffff, 1, 0, 0, 0, 0, 0, 0, 0, 0);
    $display("Spike physical attribute interval checks passed");
    $finish;
  end
endmodule
