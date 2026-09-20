// Exhaustively checks zero, one-hot, and multi-hot five-lane priority encoding.
// SPDX-License-Identifier: Apache-2.0
module priority_encoder_tb;
  logic [4:0] requests;
  logic [2:0] selected_index;
  logic [4:0] selected_oh;
  logic selected_two_index;
  logic [1:0] selected_two_oh;
  logic selected_single_index;
  logic selected_single_oh;

  PriorityEncoderFive dut (
    .requests              (requests),
    .selected_index        (selected_index),
    .selected_oh           (selected_oh),
    .selected_two_index    (selected_two_index),
    .selected_two_oh       (selected_two_oh),
    .selected_single_index (selected_single_index),
    .selected_single_oh    (selected_single_oh)
  );

  task automatic check(input logic [4:0] value);
    logic [2:0] expected_index;
    logic [4:0] expected_oh;
    logic found;
    expected_index = 3'b000;
    expected_oh = 5'b00000;
    found = 1'b0;
    for (int index = 0; index < 5; index++) begin
      if (value[index] && !found) begin
        expected_index = 3'(index);
        expected_oh[index] = 1'b1;
        found = 1'b1;
      end
    end
    requests = value;
    #1;
    assert (selected_index == expected_index)
      else $fatal(1, "priority encoder selected the wrong index for %b", value);
    assert (selected_oh == expected_oh)
      else $fatal(1, "one-hot priority encoder selected the wrong lane for %b", value);
    assert (selected_two_index == (value[0] ? 1'b0 : value[1]))
      else $fatal(1, "two-lane priority encoder selected the wrong index for %b", value[1:0]);
    assert (selected_two_oh == (value[0] ? 2'b01 : {value[1], 1'b0}))
      else $fatal(1, "two-lane one-hot encoder selected the wrong lane for %b", value[1:0]);
    assert (selected_single_index == 1'b0)
      else $fatal(1, "single-lane priority encoder did not return index zero");
    assert (selected_single_oh == value[0])
      else $fatal(1, "single-lane one-hot encoder did not preserve its input");
  endtask

  initial begin
    for (int value = 0; value < 32; value++) begin
      check(5'(value));
    end
    $finish;
  end
endmodule
