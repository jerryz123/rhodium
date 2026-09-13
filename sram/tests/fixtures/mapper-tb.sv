// Checks that generated OpenRAM wrappers preserve banking, width slicing, and byte-write semantics.
// SPDX-License-Identifier: Apache-2.0
module mapper_tb;
  reg clk = 1'b0;
  reg [9:0] address = 10'b0;
  reg enable = 1'b0;
  reg write_mode = 1'b0;
  reg [39:0] write_data = 40'b0;
  reg [4:0] write_mask = 5'b0;
  wire [39:0] read_data;

  storage_1024x40 dut (
    .RW0_addr(address),
    .RW0_en(enable),
    .RW0_clk(clk),
    .RW0_wmode(write_mode),
    .RW0_wdata(write_data),
    .RW0_rdata(read_data),
    .RW0_wmask(write_mask)
  );

  always #5 clk = ~clk;

  task write_word(input [9:0] target, input [39:0] data, input [4:0] mask);
    begin
      @(negedge clk);
      address = target;
      write_data = data;
      write_mask = mask;
      write_mode = 1'b1;
      enable = 1'b1;
      @(posedge clk);
      #1 enable = 1'b0;
    end
  endtask

  task check_word(input [9:0] target, input [39:0] expected);
    begin
      @(negedge clk);
      address = target;
      write_mode = 1'b0;
      enable = 1'b1;
      @(posedge clk);
      #1;
      if (read_data !== expected)
        $fatal(1, "address %0d: got %h, expected %h", target, read_data, expected);
      enable = 1'b0;
    end
  endtask

  initial begin
    write_word(10'd0, 40'h12_3456789a, 5'b11111);
    write_word(10'd512, 40'hab_cdef0123, 5'b11111);
    check_word(10'd0, 40'h12_3456789a);
    check_word(10'd512, 40'hab_cdef0123);
    write_word(10'd512, 40'h55_66778899, 5'b00101);
    check_word(10'd512, 40'hab_cd770199);
    check_word(10'd0, 40'h12_3456789a);
    $display("mapper functional test PASS");
    $finish;
  end
endmodule
