// Simulates every valid selector encoding through the typed one-hot example.
// SPDX-License-Identifier: Apache-2.0
module one_hot_tb;
  logic [3:0] current;
  logic [3:0] next_grant;
  logic [3:0] current_bits;
  logic       is_last;

  OneHotRotate dut (
    .current      (current),
    .next_grant   (next_grant),
    .current_bits (current_bits),
    .is_last      (is_last)
  );

  task automatic check(
    input logic [3:0] value,
    input logic [3:0] expected_next,
    input logic       expected_last
  );
    current = value;
    #1;
    assert (current_bits == value)
      else $fatal(1, "one-hot representation cast changed the bits");
    assert (next_grant == expected_next)
      else $fatal(1, "one-hot lookup produced the wrong next value");
    assert (is_last == expected_last)
      else $fatal(1, "one-hot equality produced the wrong result");
  endtask

  initial begin
    check(4'b0001, 4'b0010, 1'b0);
    check(4'b0010, 4'b0100, 1'b0);
    check(4'b0100, 4'b1000, 1'b0);
    check(4'b1000, 4'b0001, 1'b1);
    $finish;
  end
endmodule
