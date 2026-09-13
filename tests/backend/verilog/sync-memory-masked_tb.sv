// Simulates byte-mask selection on a shared synchronous read-write memory port.
// SPDX-License-Identifier: Apache-2.0
module sync_memory_masked_tb;
  logic clock = 1'b0;
  logic reset = 1'b0;
  logic [1:0] address;
  logic enable;
  logic write;
  logic [31:0] write_data;
  logic [3:0] write_mask;
  logic [31:0] read_data;

  MaskedSyncMemoryExample dut (
    .clock(clock),
    .reset(reset),
    .address(address),
    .enable(enable),
    .write(write),
    .write_data(write_data),
    .write_mask(write_mask),
    .read_data(read_data)
  );

  always #5 clock = ~clock;

  task automatic read_and_expect(input logic [31:0] expected);
    write = 1'b0;
    @(posedge clock);
    #1;
    assert (read_data == expected)
      else $fatal(1, "masked synchronous memory read %h, expected %h",
                  read_data, expected);
  endtask

  initial begin
    address = 2'd1;
    enable = 1'b1;
    write = 1'b1;
    write_data = 32'hAABBCCDD;
    write_mask = 4'b1111;
    @(posedge clock);
    #1;
    read_and_expect(32'hAABBCCDD);

    write = 1'b1;
    write_data = 32'h11223344;
    write_mask = 4'b0101;
    @(posedge clock);
    #1;
    read_and_expect(32'hAA22CC44);

    write = 1'b1;
    write_data = 32'hFFFFFFFF;
    write_mask = 4'b0000;
    @(posedge clock);
    #1;
    read_and_expect(32'hAA22CC44);

    write = 1'b1;
    write_data = 32'h55667788;
    write_mask = 4'b1111;
    @(posedge clock);
    #1;
    read_and_expect(32'h55667788);

    enable = 1'b0;
    $display("masked synchronous memory simulation passed");
    $finish;
  end
endmodule
