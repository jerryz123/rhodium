// Requires a labeled failure from an assertion inside a nested retained memory provider.
// SPDX-License-Identifier: Apache-2.0
module retained_memory_fail_tb;
  logic clock = 0, reset = 1;
  logic [7:0] write_data = 255;
  logic write_enable = 1;
  MemoryFailure dut(.*);
  always #5 clock = ~clock;
  initial begin
    repeat (2) @(posedge clock);
    #1 reset = 0;
    @(posedge clock);
    #1 $finish;
  end
endmodule
