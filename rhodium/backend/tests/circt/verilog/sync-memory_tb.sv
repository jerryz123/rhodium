// Simulates explicit synchronous-memory read and write port timing.
// SPDX-License-Identifier: Apache-2.0
module sync_memory_tb;
  logic clock = 1'b0;
  logic reset = 1'b0;
  logic [1:0] read_address;
  logic read_enable;
  logic [1:0] write_address;
  logic [7:0] write_data;
  logic write_enable;
  logic [7:0] read_data;

  SyncMemoryExample dut (
    .clock(clock),
    .reset(reset),
    .read_address(read_address),
    .read_enable(read_enable),
    .write_address(write_address),
    .write_data(write_data),
    .write_enable(write_enable),
    .read_data(read_data)
  );

  always #5 clock = ~clock;

  initial begin
    read_address = 2'd0;
    read_enable = 1'b0;
    write_address = 2'd1;
    write_data = 8'hA5;
    write_enable = 1'b1;
    @(posedge clock);
    #1;

    write_enable = 1'b0;
    read_address = 2'd1;
    read_enable = 1'b1;
    @(posedge clock);
    #1;
    assert (read_data == 8'hA5)
      else $fatal(1, "synchronous read did not return stored data after the edge");

    read_address = 2'd2;
    #1;
    assert (read_data == 8'hA5)
      else $fatal(1, "synchronous read changed without a clock edge");

    write_address = 2'd2;
    write_data = 8'h5A;
    write_enable = 1'b1;
    read_enable = 1'b0;
    @(posedge clock);
    #1;

    write_enable = 1'b0;
    read_enable = 1'b1;
    @(posedge clock);
    #1;
    assert (read_data == 8'h5A)
      else $fatal(1, "second synchronous read returned the wrong data");

    $display("synchronous memory simulation passed");
    $finish;
  end
endmodule
