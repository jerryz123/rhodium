// Simulates mutually exclusive reads and writes through one synchronous memory port.
// SPDX-License-Identifier: Apache-2.0
module sync_memory_1rw_tb;
  logic clock = 1'b0;
  logic reset = 1'b0;
  logic [1:0] address;
  logic enable;
  logic write;
  logic [7:0] write_data;
  logic [7:0] read_data;

  SyncMemory1RWExample dut (
    .clock(clock),
    .reset(reset),
    .address(address),
    .enable(enable),
    .write(write),
    .write_data(write_data),
    .read_data(read_data)
  );

  always #5 clock = ~clock;

  initial begin
    address = 2'd1;
    enable = 1'b1;
    write = 1'b1;
    write_data = 8'hA5;
    @(posedge clock);
    #1;

    write = 1'b0;
    @(posedge clock);
    #1;
    assert (read_data == 8'hA5)
      else $fatal(1, "shared-port read did not return the first write");

    address = 2'd2;
    write = 1'b1;
    write_data = 8'h5A;
    @(posedge clock);
    #1;

    write = 1'b0;
    @(posedge clock);
    #1;
    assert (read_data == 8'h5A)
      else $fatal(1, "shared-port read did not return the second write");

    enable = 1'b0;
    @(posedge clock);
    #1;

    $display("shared synchronous memory simulation passed");
    $finish;
  end
endmodule
