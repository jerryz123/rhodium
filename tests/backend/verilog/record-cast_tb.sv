// Simulates canonical record packing order and a Bits-to-record round trip.
// SPDX-License-Identifier: Apache-2.0
module record_cast_tb;
  typedef struct packed {
    logic [7:0] left;
    logic [7:0] right;
  } pair_t;

  pair_t source;
  logic [15:0] packed_bits;
  pair_t restored;

  BundleCast dut (
    .source(source),
    .bits_out(packed_bits),
    .restored(restored)
  );

  initial begin
    source = '{left: 8'h12, right: 8'h34};
    #1;

    if (packed_bits !== 16'h1234)
      $fatal(1, "first record field must occupy the most-significant bits");
    if (restored.left !== 8'h12 || restored.right !== 8'h34)
      $fatal(1, "record cast round trip failed");

    $display("record cast simulation passed");
    $finish;
  end
endmodule
