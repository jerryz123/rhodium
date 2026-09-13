// Exhaustively checks lane-mask expansion, single enables, slices, and non-byte lane widths.
// SPDX-License-Identifier: Apache-2.0
module expand_mask_tb;
  logic [7:0] mask;
  logic single;
  wire [7:0] lanes_1;
  wire [23:0] lanes_3;
  wire [63:0] lanes_8;
  wire single_1;
  wire [6:0] single_7;
  wire [19:0] sliced_5;

  MaskExpansionFixture dut(.*);

  initial begin
    for (int pattern = 0; pattern < 256; pattern++) begin
      for (int enabled = 0; enabled < 2; enabled++) begin
        mask = 8'(pattern);
        single = 1'(enabled);
        #1;
        assert (lanes_1 === mask && single_1 === single)
          else $fatal(1, "unit-width expansion changed the mask");
        for (int bit_index = 0; bit_index < 24; bit_index++)
          assert (lanes_3[bit_index] === mask[bit_index / 3])
            else $fatal(1, "three-bit lane ordering: pattern=%h bit=%0d", mask, bit_index);
        for (int bit_index = 0; bit_index < 64; bit_index++)
          assert (lanes_8[bit_index] === mask[bit_index / 8])
            else $fatal(1, "byte lane ordering: pattern=%h bit=%0d", mask, bit_index);
        for (int bit_index = 0; bit_index < 7; bit_index++)
          assert (single_7[bit_index] === single)
            else $fatal(1, "single enable did not fill its lane");
        for (int bit_index = 0; bit_index < 20; bit_index++)
          assert (sliced_5[bit_index] === mask[2 + bit_index / 5])
            else $fatal(1, "sliced mask ordering: pattern=%h bit=%0d", mask, bit_index);
      end
    end
    $display("mask expansion passed: 512 input combinations");
    $finish;
  end
endmodule
