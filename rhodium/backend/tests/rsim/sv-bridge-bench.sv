// SPDX-License-Identifier: Apache-2.0
// Match the production harness interface without modifying its TestDriver.
module SoCHarness(input logic clock, reset, output wire [31:0] exit);
  wire [7:0] count;
  bit fault;
  initial fault = $test$plusargs("bridge-fail");
  RsimBridgeCounter core(.clock(clock), .reset(reset), .step(8'd1),
                        .fault(fault), .count(count), .exit(exit));
endmodule

module BridgeBench;
  bit clock = 0;
  bit first_reset = 1, second_reset = 1;
  byte unsigned first_step = 1, second_step = 3;
  wire [7:0] first_count, second_count;
  wire [31:0] first_exit, second_exit;
  byte unsigned sampled_first, sampled_second;
  int unsigned expected_first = 0, expected_second = 0;
  int cycle = 0;
  bit initialized = 0;
  RsimBridgeCounter first(.clock(clock), .reset(first_reset), .step(first_step),
      .fault(1'b0), .count(first_count), .exit(first_exit));
  RsimBridgeCounter second(.clock(clock), .reset(second_reset), .step(second_step),
      .fault(1'b0), .count(second_count), .exit(second_exit));

  // An external synchronous consumer must see the pre-edge outputs, regardless
  // of the order in which Verilator executes the wrapper and this process.
  always @(posedge clock) begin
    sampled_first <= first_count;
    sampled_second <= second_count;
  end

  task automatic advance(input bit r1, r2, input byte unsigned s1, s2);
    int unsigned old_first, old_second;
    old_first = expected_first;
    old_second = expected_second;
    clock = 0;
    first_reset = r1; second_reset = r2;
    first_step = s1; second_step = s2;
    #1;
    if (initialized && (int'(first_count) != old_first || int'(second_count) != old_second))
      $fatal(1, "outputs changed without an edge");
    clock = 1;
    #1;
    if (initialized && (int'(sampled_first) != old_first || int'(sampled_second) != old_second))
      $fatal(1, "outputs published before NBA");
    expected_first = r1 ? 0 : (old_first + int'(s1)) % 256;
    expected_second = r2 ? 0 : (old_second + int'(s2)) % 256;
    if (int'(first_count) != expected_first || int'(second_count) != expected_second ||
        first_exit != 32'(expected_first == 4) || second_exit != 32'(expected_second == 4))
      $fatal(1, "counter oracle mismatch");
    $display("STATE %0d %0d %0d %0d %0d", cycle++, first_count, second_count, first_exit, second_exit);
    initialized = 1;
  endtask

  initial begin
    advance(1, 1, 1, 3);
    advance(0, 1, 1, 3);
    advance(0, 0, 3, 4);
    // Independent mid-run resets and wraparound distinguish model ownership.
    for (int index = 0; index < 20; index++)
      advance(index == 4, index == 9, 8'(index + 17), 8'(index + 53));
    $display("BRIDGE_BENCH_PASS");
    $finish;
  end
endmodule

// Non-native widths, bit 63, authored keywords/private-name collisions, and a
// nonstandard clock name exercise generic port conversion and initial constants.
module BridgePortsBench;
  bit clock = 0, reset = 1;
  logic [63:0] data = 0;
  wire out1;
  wire [4:0] out5;
  wire [30:0] out31;
  wire [32:0] out33;
  wire [62:0] out63;
  wire [63:0] out64, constant_value;
  RsimBridgePorts dut(.\edge (clock), .reset(reset), .flag(data[0]), ._rsim1(data[4:0]),
      .word31(data[30:0]), .word33(data[32:0]), .word63(data[62:0]), .\class (data),
      .out1(out1), .out5(out5), .out31(out31), .out33(out33), .out63(out63), .out64(out64), ._rsim2(constant_value));
  initial begin
    #1;
    if (constant_value != 64'h8000000000000007) $fatal(1, "constant missing before first edge");
    for (int index = 0; index < 12; index++) begin
      clock = 0;
      reset = index == 0 || index == 7;
      data = 64'hfedcba9876543210 ^ (64'h8000000100000001 * 64'(index));
      #1;
      clock = 1;
      #1;
      if (out1 != (reset ? 1'b0 : data[0]) || out5 != (reset ? 5'd0 : data[4:0]) ||
          out31 != (reset ? 31'd0 : data[30:0]) || out33 != (reset ? 33'd0 : data[32:0]) ||
          out63 != (reset ? 63'd0 : data[62:0]) || out64 != (reset ? 64'd0 : data))
        $fatal(1, "scalar bridge conversion mismatch");
      $display("STATE %0d %h %h %h %h %h %h", index, out1, out5, out31, out33, out63, out64);
    end
    $display("BRIDGE_PORTS_PASS");
    $finish;
  end
endmodule
