// Checks RV32 scalar load/store lane generation within one 32-bit cache word.
// SPDX-License-Identifier: Apache-2.0
module load_store_rv32_word_tb;
  localparam logic [1:0] BYTE = 2'd0;
  localparam logic [1:0] HALF = 2'd1;
  localparam logic [1:0] WORD = 2'd2;

  logic [31:0] address;
  logic [31:0] load_data;
  logic [31:0] store_value;
  logic [1:0] width;
  logic unsigned_load;
  logic aligned;
  logic [31:0] load_value;
  logic [31:0] store_data;
  logic [3:0] store_mask;

  LoadStoreFixture dut (.*);

  task automatic check_access(
    input logic [1:0] access_width,
    input logic [1:0] offset,
    input logic [31:0] expected_signed,
    input logic [31:0] expected_unsigned,
    input logic [3:0] expected_mask
  );
    begin
      address = 32'h1000 + {30'b0, offset};
      width = access_width;
      unsigned_load = 1'b0;
      #1;
      assert (aligned && load_value == expected_signed &&
              store_data == (store_value << (offset * 8)) &&
              store_mask == expected_mask)
        else $fatal(1, "RV32 signed access failed width=%0d offset=%0d", access_width, offset);
      unsigned_load = 1'b1;
      #1;
      assert (load_value == expected_unsigned)
        else $fatal(1, "RV32 unsigned access failed width=%0d offset=%0d", access_width, offset);
    end
  endtask

  initial begin
    load_data = 32'h80ff_7f01;
    store_value = 32'h4433_2211;
    check_access(BYTE, 2'd0, 32'h0000_0001, 32'h0000_0001, 4'b0001);
    check_access(BYTE, 2'd3, 32'hffff_ff80, 32'h0000_0080, 4'b1000);
    check_access(HALF, 2'd0, 32'h0000_7f01, 32'h0000_7f01, 4'b0011);
    check_access(HALF, 2'd2, 32'hffff_80ff, 32'h0000_80ff, 4'b1100);
    check_access(WORD, 2'd0, 32'h80ff_7f01, 32'h80ff_7f01, 4'b1111);

    address = 32'h1001;
    width = HALF;
    #1;
    assert (!aligned) else $fatal(1, "misaligned RV32 halfword was accepted");

    $display("RV32 XLEN-word load/store lane generation passed");
    $finish;
  end
endmodule
