// Simulates a record-valued mux, synchronous-reset register, and output port.
// SPDX-License-Identifier: Apache-2.0
module bundle_tb;
  typedef struct packed {
    logic [7:0] left;
    logic [7:0] right;
  } pair_t;

  logic clk = 0;
  logic reset = 0;
  logic select = 0;
  pair_t direct;
  pair_t alternate;
  pair_t result;

  BundlePipeline dut (
    .clk(clk),
    .reset(reset),
    .select(select),
    .direct(direct),
    .alternate(alternate),
    .result(result)
  );

  task tick;
    begin
      #1 clk = 1;
      #1 clk = 0;
    end
  endtask

  initial begin
    direct = '{left: 8'h12, right: 8'h34};
    alternate = '{left: 8'h56, right: 8'h78};

    reset = 1;
    tick();
    reset = 0;
    if (result.left !== 8'h00 || result.right !== 8'h00)
      $fatal(1, "record reset failed");

    select = 1;
    tick();
    if (result.left !== 8'h12 || result.right !== 8'h34)
      $fatal(1, "record true selection failed");

    select = 0;
    tick();
    if (result.left !== 8'h56 || result.right !== 8'h78)
      $fatal(1, "record false selection failed");

    $display("bundle simulation passed");
    $finish;
  end
endmodule
