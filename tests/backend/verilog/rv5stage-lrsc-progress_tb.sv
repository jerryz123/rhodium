// Exercises real L1Ds and inclusive LLC eviction against LR/SC and competing writes.
module rv5stage_lrsc_progress_tb;
  typedef struct packed { logic ready; } ready_t;
  typedef struct packed { logic valid; RV5StageDataReq bits; } request_t;
  typedef struct packed { logic valid; RV5StageDataResp bits; } response_t;
  typedef struct packed { request_t request; } core_in_t;
  typedef struct packed { ready_t request; logic request_fault; logic request_access_fault; response_t response; logic drained; logic reservation_valid; } core_out_t;
  logic clock = 0, reset = 1;
  core_in_t [1:0] core_in;
  core_out_t [1:0] core_out;
  logic eviction_waiting;
  integer completions[2] = '{0, 0};
  logic [63:0] results[2];
  RV5StageLRSCProgress dut (
    .clock(clock), .reset(reset), .eviction_waiting(eviction_waiting),
    .core_0_in(core_in[0]), .core_0_out(core_out[0]),
    .core_1_in(core_in[1]), .core_1_out(core_out[1])
  );
  always #5 clock = ~clock;

  task automatic tick;
    for (int client = 0; client < 2; client++) begin
      if (!reset && core_out[client].response.valid) begin
        assert (!core_out[client].response.bits.access_fault) else $fatal(1, "unexpected cache fault");
        completions[client]++;
        results[client] = core_out[client].response.bits.data;
      end
    end
    @(posedge clock);
    #1;
  endtask

  task automatic issue(input int client, input logic [3:0] operation,
                       input logic [63:0] address, input logic [63:0] data = 0);
    for (int cycle = 0; !core_out[client].request.ready && cycle < 500; cycle++) tick();
    assert (core_out[client].request.ready) else $fatal(1, "request admission timeout");
    core_in[client].request.bits = '0;
    core_in[client].request.bits.address = address;
    core_in[client].request.bits.access = operation;
    core_in[client].request.bits.width = 3;
    core_in[client].request.bits.data = data;
    core_in[client].request.bits.destination = operation == 2 ? 0 : 1;
    core_in[client].request.valid = 1;
    tick();
    core_in[client].request.valid = 0;
  endtask

  task automatic await_count(input int client, input int target, input logic [63:0] value,
                             input bit check_value = 1);
    for (int cycle = 0; completions[client] < target && cycle < 1000; cycle++) tick();
    assert (completions[client] == target && (!check_value || results[client] == value))
      else $fatal(1, "client %0d completion %0d/%0d data %h expected %h", client, completions[client], target, results[client], value);
  endtask

  initial begin
    int left_count, right_count, first, second;
    int baseline[2];
    core_in = '0;
    repeat (2) tick();
    reset = 0;
    tick();
    // Initialize the architectural word through the coherent path, not SRAM
    // hierarchy. Other words are read only to cause replacement; their initial
    // data is deliberately not asserted.
    issue(0, 2, 0, 1);
    await_count(0, 1, 0);
    for (int iteration = 0; iteration < 12; iteration++) begin
      left_count = completions[0];
      right_count = completions[1];
      issue(0, 3, 0);
      await_count(0, left_count + 1, 64'(iteration + 1));
      assert (core_out[0].reservation_valid) else $fatal(1, "LR did not reserve");
      // All addresses conflict in the one-way LLC, including a line still
      // resident in L1D. The other client only reads: no external write can
      // excuse failure to make progress on the reserved word.
      issue(1, 1, 64'(128 + iteration * 128));
      for (int cycle = 0; !eviction_waiting && cycle < 60; cycle++) tick();
      assert (eviction_waiting) else $fatal(1, "test did not reach inclusive eviction pressure");
      repeat (40) begin
        assert (eviction_waiting && core_out[0].reservation_valid)
          else $fatal(1, "inclusive eviction revoked the protected reservation");
        tick();
      end
      issue(0, 4, 0, 64'(iteration + 2));
      await_count(0, left_count + 2, 0);
      await_count(1, right_count + 1, 0, 0);
    end
    // The last dirty SC value must survive LLC eviction and SRAM writeback.
    left_count = completions[0];
    issue(0, 1, 0);
    await_count(0, left_count + 1, 13);

    // A stalled reserving client must eventually let a conflicting writer
    // acquire the line. Its subsequently expired SC must not overwrite it.
    left_count = completions[0];
    right_count = completions[1];
    issue(0, 3, 0);
    await_count(0, left_count + 1, 13);
    issue(1, 2, 0, 99);
    await_count(1, right_count + 1, 0);
    assert (!core_out[0].reservation_valid) else $fatal(1, "writer completed without revoking reservation");
    issue(0, 4, 0, 77);
    await_count(0, left_count + 2, 1);
    issue(0, 1, 0);
    await_count(0, left_count + 3, 99);
    // Simultaneous contenders must not both reserve a shared copy and then
    // deadlock upgrading at SC. Whichever obtains LR ownership first can
    // complete its local SC while the other acquisition waits at Home.
    baseline[0] = completions[0];
    baseline[1] = completions[1];
    issue(0, 3, 0);
    issue(1, 3, 0);
    for (int cycle = 0; completions[0] == baseline[0] && completions[1] == baseline[1] && cycle < 1000; cycle++) tick();
    assert (completions[0] != baseline[0] || completions[1] != baseline[1])
      else $fatal(1, "competing LR requests made no progress");
    first = completions[0] != baseline[0] ? 0 : 1;
    second = 1 - first;
    await_count(first, baseline[first] + 1, 99);
    issue(first, 4, 0, 100);
    await_count(first, baseline[first] + 2, 0);
    await_count(second, baseline[second] + 1, 100);
    issue(second, 4, 0, 101);
    await_count(second, baseline[second] + 2, 0);
    left_count = completions[0];
    issue(0, 1, 0);
    await_count(0, left_count + 1, 101);
    $display("Two-cache LR/SC progress passed: 12 read-only LLC evictions, dirty-data preservation, stalled-core expiry, intervening write, competing LR/SC");
    $finish;
  end
  initial begin
    #2000000;
    $fatal(1, "LR/SC integration watchdog expired");
  end
endmodule
