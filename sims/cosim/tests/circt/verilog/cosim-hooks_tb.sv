// Checks clocked architectural hooks with a post-evaluation collection boundary.
// SPDX-License-Identifier: Apache-2.0
module cosim_hooks_tb;
  bit clock = 0;
  bit [3:0] step = 0;
  CosimHooksFixture dut(.*);
  import "DPI-C" function void cosim_test_begin(input longint sample);
  import "DPI-C" function void cosim_test_end(input longint sample);
  initial begin
    for (int i = 0; i < 5; i++) begin
      step = 4'(i);
      cosim_test_begin(64'(i));
      #1 clock = 1;
      #1 cosim_test_end(64'(i));
      clock = 0;
    end
    $finish;
  end
endmodule
