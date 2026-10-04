// SPDX-License-Identifier: Apache-2.0
module SoCHarness(input clock, input reset, output logic [31:0] exit
`ifdef TEST_TRACE
                 , output wire __event_activity
`endif
);
  import "DPI-C" function void runtime_test_edge(input bit reset_active);
  integer cycles = 0;
`ifdef TEST_TRACE
  assign __event_activity = clock;
`endif
  always @(posedge clock) begin
    runtime_test_edge(reset);
    if (reset) begin
      cycles <= 0;
      exit <= 0;
    end else begin
      cycles <= cycles + 1;
      if (cycles == 4) exit <= $test$plusargs("runtime-test-target-fail") ? 3 : 1;
    end
  end
endmodule
