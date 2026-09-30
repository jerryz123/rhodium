// Scores asynchronous reads, clocked writes, independent memories, and reset-suppressed assertions.
// SPDX-License-Identifier: Apache-2.0
module retained_memory_tb;
  logic clock = 0, reset = 1;
  logic [1:0] read_address = 0, write_address = 0;
  logic [7:0] data_a = 0, data_b = 0;
  logic enable_a = 0, enable_b = 0, clear_a = 0;
  wire [7:0] result_a, result_b, eager_a, eager_b;
  logic [7:0] model_a[4], model_b[4];
  bit known_a[4], known_b[4];
  MemoryComparison dut(.*);

  task automatic check_outputs;
    if (known_a[read_address] && (result_a !== model_a[read_address] || eager_a !== model_a[read_address]))
      $fatal(1, "memory A mismatch at %0d", read_address);
    if (known_b[read_address] && (result_b !== model_b[read_address] || eager_b !== model_b[read_address]))
      $fatal(1, "memory B mismatch at %0d", read_address);
  endtask

  task automatic tick;
    #2; check_outputs(); // Same-address reads see old storage before the edge.
    clock = 1;
    if (enable_a) begin model_a[write_address] = data_a; known_a[write_address] = 1; end
    if (enable_b) begin model_b[write_address] = data_b; known_b[write_address] = 1; end
    #2; check_outputs();
    clock = 0;
    #2;
  endtask

  initial begin
    // Reset does not initialize memory or suppress writes. Initialize every
    // location explicitly, using distinct values to expose occurrence aliasing.
    enable_a = 1; enable_b = 1;
    for (int i = 0; i < 4; i++) begin
      write_address = 2'(i); read_address = 2'(i);
      data_a = 8'(i + 10); data_b = 8'(i + 90);
      tick();
    end
    reset = 0;
    for (int i = 0; i < 36; i++) begin
      write_address = 2'(i); read_address = write_address;
      data_a = 8'(i * 3 + 20); data_b = 8'(i * 5 + 40);
      enable_a = (i % 3 != 0); enable_b = (i % 4 != 0);
      tick();
      read_address = 2'(i + 1);
      #2; check_outputs(); // Read address changes without a clock edge.
    end
    // A false assertion condition is ignored with a disabled guard.
    data_a = 255; data_b = 255; enable_a = 0; enable_b = 0;
    tick();
    // Scoped reset suppresses A's nested assertion, without clearing memory.
    clear_a = 1; enable_a = 1;
    tick();
    clear_a = 0; enable_a = 0;
    read_address = write_address;
    #2; check_outputs();
    // Global reset suppresses both assertions; enabled writes still happen.
    reset = 1; enable_a = 1; enable_b = 1;
    tick();
    reset = 0; enable_a = 0; enable_b = 0;
    for (int i = 0; i < 4; i++) begin
      read_address = 2'(i);
      #2; check_outputs();
    end
    $display("retained memory: scoreboard and eager equivalence passed");
    $finish;
  end
endmodule
