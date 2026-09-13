// Simulates a lexically nested child circuit and its captured host width.
// SPDX-License-Identifier: Apache-2.0
module nested_circuit_tb;
  logic [7:0] value_in;
  logic [7:0] value_out;

  Incrementer dut (
    .value_in  (value_in),
    .value_out (value_out)
  );

  initial begin
    value_in = 8'h00;
    #1;
    assert (value_out == 8'h01)
      else $fatal(1, "nested incrementer failed at zero");

    value_in = 8'h7f;
    #1;
    assert (value_out == 8'h80)
      else $fatal(1, "nested incrementer failed at the midpoint");

    value_in = 8'hff;
    #1;
    assert (value_out == 8'h00)
      else $fatal(1, "nested incrementer did not preserve modular width");

    $finish;
  end
endmodule
