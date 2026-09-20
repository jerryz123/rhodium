// Exercises output-greedy one-to-one grants, input rotation, and stalled priorities.
// SPDX-License-Identifier: Apache-2.0
module round_robin_matcher_tb;
  logic clock = 1'b0;
  logic reset = 1'b1;
  logic [2:0][1:0] requests;
  logic [1:0] accepts;
  logic [2:0][1:0] grants;

  OutputGreedyRoundRobinMatcher dut (.*);
  always #5 clock = ~clock;
  task automatic tick; @(posedge clock); #1; endtask

  task automatic check_grants(
      input logic [1:0] expected_0,
      input logic [1:0] expected_1,
      input logic [1:0] expected_2);
    assert (grants[0] == expected_0 &&
            grants[1] == expected_1 &&
            grants[2] == expected_2)
      else $fatal(1, "unexpected round-robin matching: %b %b %b",
                  grants[0], grants[1], grants[2]);
  endtask

  initial begin
    requests = '0;
    accepts = '0;
    tick();
    reset = 1'b0;

    // Both outputs begin at input zero. Output one skips the input already
    // claimed by output zero, producing a one-to-one maximal matching.
    requests = '1;
    accepts = 2'b11;
    #1;
    check_grants(2'b01, 2'b10, 2'b00);
    tick();
    check_grants(2'b00, 2'b01, 2'b10);
    tick();
    check_grants(2'b10, 2'b00, 2'b01);
    tick();

    // Output zero does not rotate when its selected transfer is rejected;
    // output one continues rotating independently after acceptance.
    accepts = 2'b10;
    #1;
    check_grants(2'b01, 2'b10, 2'b00);
    tick();
    check_grants(2'b01, 2'b00, 2'b10);

    // Input rotation does not override fixed output ordering. A sole input
    // requesting both outputs remains assigned to output zero.
    requests = '0;
    requests[0] = 2'b11;
    accepts = 2'b11;
    #1;
    check_grants(2'b01, 2'b00, 2'b00);
    tick();
    check_grants(2'b01, 2'b00, 2'b00);

    // Every possible request image preserves the matching contract at the
    // current priorities, irrespective of which requests are absent.
    accepts = '0;
    for (int image = 0; image < 64; image++) begin
      requests = image[5:0];
      #1;
      for (int input_index = 0; input_index < 3; input_index++) begin
        assert ((grants[input_index] & ~requests[input_index]) == 0)
          else $fatal(1, "matcher granted an unrequested output");
        assert ($onehot0(grants[input_index]))
          else $fatal(1, "matcher granted one input more than once");
      end
      for (int output_index = 0; output_index < 2; output_index++) begin
        assert ($onehot0({grants[2][output_index],
                          grants[1][output_index],
                          grants[0][output_index]}))
          else $fatal(1, "matcher granted one output more than once");
      end
    end

    $display("Round-robin matcher simulation passed");
    $finish;
  end
endmodule
