// Provides a zero-delay functional Sky130 SRAM model for mapping tests and mapped simulation.
// SPDX-License-Identifier: Apache-2.0
module sky130_sram_2kbyte_1rw1r_32x512_8(
  input wire clk0,
  input wire csb0,
  input wire web0,
  input wire [3:0] wmask0,
  input wire [8:0] addr0,
  input wire [31:0] din0,
  output reg [31:0] dout0,
  input wire clk1,
  input wire csb1,
  input wire [8:0] addr1,
  output reg [31:0] dout1
);
  reg [31:0] storage [0:511];
  integer lane;

  always @(posedge clk0) begin
    if (!csb0) begin
      if (web0)
        dout0 <= storage[addr0];
      else
        for (lane = 0; lane < 4; lane = lane + 1)
          if (wmask0[lane])
            storage[addr0][lane * 8 +: 8] <= din0[lane * 8 +: 8];
    end
  end

  always @(posedge clk1)
    if (!csb1)
      dout1 <= storage[addr1];
endmodule
