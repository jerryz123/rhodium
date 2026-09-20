// Checks immutable bundle replacement, untouched metadata, and method propagation.
// SPDX-License-Identifier: Apache-2.0
module bundle_update_tb;
  logic clock = 0, reset = 0, choose = 0;
  logic [13:0] source = 0;
  logic [7:0] data = 0;
  logic [3:0] tag = 0;
  wire [13:0] original, changed, registered, inferred, remembered, observed;
  wire [11:0] swapped;
  wire [8:0] plain;
  wire [12:0] tagged_payload;
  wire [8:0] methods;
  logic [13:0] previous;

  BundleUpdateFixture dut(.*);

  task tick;
    #1 clock = 1;
    #1 clock = 0;
  endtask

  initial begin
    reset = 1;
    tick();
    reset = 0;
    tick();
    previous = source;
    for (int i = 0; i < 256; i++) begin
      source = 14'((i * 73) ^ (i << 5));
      data = 8'(255 - i);
      tag = 4'(i);
      choose = i[0];
      #1;
      if (original !== source) $fatal(1, "update mutated original");
      if (changed !== {data, tag, source[1:0]}) $fatal(1, "nested replacement");
      if (swapped !== source[13:2]) $fatal(1, "structural record replacement");
      if (plain !== {data, source[1]}) $fatal(1, "absent optional field");
      if (tagged_payload !== {data, tag, source[1]}) $fatal(1, "chained replacement");
      if (registered !== {previous[13:1], source[0]}) $fatal(1, "register replacement");
      if (inferred !== {previous[13:1], source[0]}) $fatal(1, "inferred register replacement");
      if (remembered !== {previous[13:1], source[0]}) $fatal(1, "memory replacement");
      if (observed !== {source[13:1], 1'b1}) $fatal(1, "observed interface replacement");
      if (methods !== {source[1], source[1], previous[1], source[1], source[1],
                       (choose & source[1]), previous[1], source[1], source[1]})
        $fatal(1, "updated value lost its method or field surface");
      tick();
      previous = source;
    end
    $display("immutable bundle update simulation passed");
    $finish;
  end
endmodule
