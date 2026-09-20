// Exhaustively checks transfer containment, zero lengths, and address-space-end arithmetic.
// SPDX-License-Identifier: Apache-2.0
module transfer_range_tb;
  logic [3:0] address, length, base;
  wire [15:0] contained;
  TransferRangeFixture dut(.*);

  initial begin
    for (int a = 0; a < 16; a++) begin
      for (int n = 0; n < 16; n++) begin
        for (int b = 0; b < 16; b++) begin
          address = 4'(a);
          length = 4'(n);
          base = 4'(b);
          #1;
          for (int size = 1; size <= 16; size++) begin
            assert (contained[size-1] === ((n > 0) && (a >= b) &&
                    (a+n <= b+size) && (b+size <= 16)))
              else $fatal(1, "containment: address=%0d length=%0d base=%0d window=%0d", a, n, b, size);
          end
        end
      end
    end
    $display("transfer range passed: 65536 exhaustive comparisons");
    $finish;
  end
endmodule
