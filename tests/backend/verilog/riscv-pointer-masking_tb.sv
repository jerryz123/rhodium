// Sweeps PMM, effective privilege, MXR, translation, tags, and width specialization.
module riscv_pointer_masking_tb;
  logic [63:0] address, mstatus, satp, senvcfg;
  logic [1:0] privilege, effective;
  logic [2:0] policy;
  logic [63:0] masked, warl, disabled_warl, disabled_address;
  logic [31:0] rv32_warl, rv32_address;
  PointerMaskingFixture dut (.*);

  initial begin
    for (int mode = 0; mode < 4; mode++)
      for (int priv = 0; priv < 4; priv++)
        for (int mpp = 0; mpp < 4; mpp++)
          for (int mprv = 0; mprv < 2; mprv++)
            for (int mxr = 0; mxr < 2; mxr++)
              for (int translated = 0; translated < 2; translated++)
                for (int sample_index = 0; sample_index < 12; sample_index++) begin
                  automatic int ep, length;
                  automatic logic [1:0] expected_mode;
                  automatic logic [63:0] expected;
                  privilege = 2'(priv);
                  mstatus = (64'(mpp) << 11) | (64'(mprv) << 17) | (64'(mxr) << 19);
                  satp = translated != 0 ? 64'h8000000000000000 : 0;
                  senvcfg = (64'(mode) << 32) | 64'hffff0000;
                  // Includes both signs at bit 47/56 and malformed canonical
                  // bits that masking must preserve rather than silently fix.
                  case (sample_index)
                    0: address = 64'hfe00000080000123;
                    1: address = 64'hab00800000000123;
                    2: address = 64'hab80000000000123;
                    3: address = 64'hab00008000000123;
                    4: address = 64'hffffffffffffffff;
                    5: address = 0;
                    default: address = {$urandom, $urandom};
                  endcase
                  ep = priv == 3 && mprv != 0 ? (mpp == 0 || mpp == 1 ? mpp : 3) : priv;
                  expected_mode = ep == 0 && mxr == 0 && mode != 1 ? 2'(mode) : 0;
                  length = expected_mode == 2 ? 7 : expected_mode == 3 ? 16 : 0;
                  expected = address;
                  if (length != 0) begin
                    expected = address & (64'hffffffffffffffff >> length);
                    if (translated != 0 && address[63-length]) expected |= 64'hffffffffffffffff << (64-length);
                  end
                  #1;
                  assert (effective == 2'(ep) && policy[2:1] == expected_mode && policy[0] == (translated != 0 && ep != 3))
                    else $fatal(1, "incorrect effective privilege or PMM selection");
                  assert (masked == expected && masked[47:0] == address[47:0])
                    else $fatal(1, "PMM %0d address %h got %h expected %h", mode, address, masked, expected);
                  assert (warl == (mode == 1 ? 0 : 64'(mode) << 32)) else $fatal(1, "WARL");
                  assert (disabled_warl == 0 && disabled_address == address && rv32_warl == 0 && rv32_address == address[31:0])
                    else $fatal(1, "disabled/RV32 specialization");
                end
    $display("pointer masking policy and address sweep passed");
    $finish;
  end
endmodule
