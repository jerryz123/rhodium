// Checks generic stripe edge cases and metadata-transparent CHI address projection.
// SPDX-License-Identifier: Apache-2.0
module chi_request_update_tb;
  logic [11:0] offset;
  wire [11:0] dense_single, dense_four, dense_byte, dense_stripe_only;
  CHIReqFlit source, projected, expected;
  logic valid = 0, ready = 0;
  wire result_valid, source_ready;
  CHIRequestUpdateFixture dut(.*);

  initial begin
    int global_offset, local_offset;
    for (int address = 0; address < 4096; address++) begin
      offset = 12'(address);
      #1;
      assert(dense_single == offset && dense_four == 12'((address / 256) * 64 + address % 64) &&
             dense_byte == 12'(address / 4) && dense_stripe_only == 12'(address % 1024))
        else $fatal(1, "generic stripe projection mismatch");
    end
    for (int i = 0; i < 256; i++) begin
      for (int bit_index = 0; bit_index < $bits(source); bit_index++)
        source[bit_index] = 1'($urandom);
      global_offset = (i / 4) * 256 + (i % 4) * 16;
      local_offset = (i / 4) * 64 + (i % 4) * 16;
      source.address = 52'h80000080 + 52'(global_offset);
      valid = i[0];
      ready = i[1];
      #1;
      expected = source;
      expected.address = 52'h1000 + 52'(local_offset);
      if (projected !== expected) $fatal(1, "projector changed request metadata");
      if (result_valid !== valid || source_ready !== ready) $fatal(1, "projector handshake");
    end
    $display("CHI optional-field request update simulation passed");
    $finish;
  end
endmodule
