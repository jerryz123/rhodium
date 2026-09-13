// Verifies two-write forwarding, retention, all three reads, and writable f0.
// SPDX-License-Identifier: Apache-2.0
module rv5stage_fp_register_file_tb;
  typedef struct packed {
    logic [4:0] address;
    logic [63:0] data;
  } write_bits_t;
  typedef struct packed {
    logic valid;
    write_bits_t bits;
  } write_t;

  logic clock = 1'b0;
  logic reset = 1'b1;
  logic [4:0] read_address_1;
  logic [4:0] read_address_2;
  logic [4:0] read_address_3;
  write_t first_write;
  write_t second_write;
  logic [63:0] read_data_1;
  logic [63:0] read_data_2;
  logic [63:0] read_data_3;

  RV5StageFloatingPointRegisterFile dut (
    .clock(clock),
    .reset(reset),
    .read_address_1(read_address_1),
    .read_address_2(read_address_2),
    .read_address_3(read_address_3),
    .writes_0_in(first_write),
    .writes_1_in(second_write),
    .read_data_1(read_data_1),
    .read_data_2(read_data_2),
    .read_data_3(read_data_3)
  );
  always #5 clock = ~clock;

  initial begin
    read_address_1 = 5'd0;
    read_address_2 = 5'd5;
    read_address_3 = 5'd6;
    first_write = '0;
    second_write = '0;
    repeat (2) @(posedge clock);
    #1;
    reset = 1'b0;

    first_write.valid = 1'b1;
    first_write.bits.address = 5'd0;
    first_write.bits.data = 64'h0123456789abcdef;
    second_write.valid = 1'b1;
    second_write.bits.address = 5'd6;
    second_write.bits.data = 64'hfedcba9876543210;
    #1;
    assert (read_data_1 == 64'h0123456789abcdef);
    assert (read_data_3 == 64'hfedcba9876543210);
    @(posedge clock);
    #1;
    first_write.valid = 1'b0;
    second_write.valid = 1'b0;
    assert (read_data_1 == 64'h0123456789abcdef);
    assert (read_data_3 == 64'hfedcba9876543210);

    first_write.valid = 1'b1;
    first_write.bits.address = 5'd5;
    first_write.bits.data = 64'h1111222233334444;
    #1;
    assert (read_data_2 == 64'h1111222233334444);
    @(posedge clock);
    #1;
    first_write.valid = 1'b0;
    assert (read_data_2 == 64'h1111222233334444);

    $display("RV5Stage floating-point register file passed");
    $finish;
  end
endmodule
