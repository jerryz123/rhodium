// Sweeps PMM, effective privilege, MXR, translation, tags, and width specialization.
// SPDX-License-Identifier: Apache-2.0
module riscv_pointer_masking_tb;
  logic [63:0] address, mstatus, satp, senvcfg;
  logic [1:0] privilege, effective;
  logic [2:0] policy;
  logic [63:0] masked, warl, disabled_warl, disabled_address;
  logic [31:0] rv32_warl, rv32_address;
  logic virtualized;
  logic [63:0] hstatus, vsstatus, vsatp, henvcfg, guest_masked, hstatus_warl;
  logic [1:0] access;
  logic [2:0] guest_policy;
  PointerMaskingFixture dut (.*);

  initial begin
    virtualized = 0; hstatus = 0; vsstatus = 0; vsatp = 0; henvcfg = 0; access = 0;
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
    // Independent oracle: all PMM triples, access kinds, privilege/MPRV/MPV
    // combinations, MXR controls and Bare/translated stage-one modes.
    for (int modes = 0; modes < 64; modes++)
      for (int a = 0; a < 3; a++)
        for (int ctx = 0; ctx < 64; ctx++)
          for (int translation = 0; translation < 4; translation++)
            for (int mxrs = 0; mxrs < 4; mxrs++) begin
              automatic int ep, pm, length;
              automatic bit guest, translated;
              automatic logic [63:0] expected;
              privilege = ctx[1:0] == 2 ? 3 : 2'(ctx);
              virtualized = ctx[2] && privilege != 3;
              mstatus = (64'(ctx[3]) << 17) | (64'(ctx[4]) << 39) | (64'(ctx[5] ? 1 : 0) << 11) | (64'(mxrs&1) << 19);
              hstatus = (64'(modes[5:4]) << 48) | (64'(ctx[5]) << 8);
              senvcfg = 64'(modes[1:0]) << 32; henvcfg = 64'(modes[3:2]) << 32;
              vsstatus = 64'(mxrs>>1) << 19;
              satp = 64'(translation&1) << 63; vsatp = 64'(translation>>1) << 63;
              access = 2'(a);
              address = ctx[0] ? 64'habff800040001123 : 64'hfe00000040001123;
              ep = a != 0 ? (ctx[5] ? 1 : 0) : privilege == 3 && ctx[3] ? (ctx[5] ? 1 : 0) : int'(privilege);
              guest = a != 0 || (privilege == 3 && ctx[3] ? ctx[4] && ep != 3 : virtualized);
              pm = ep == 0 ? (a != 0 && privilege == 0 ? (modes>>4)&3 : modes&3) : guest && ep == 1 ? (modes>>2)&3 : 0;
              if (pm == 1 || mxrs[0] || (guest && mxrs[1]) || a == 2) pm = 0;
              translated = ep != 3 && (guest ? vsatp[63] : satp[63]);
              length = pm == 2 ? 7 : pm == 3 ? 16 : 0;
              expected = address & (~64'd0 >> length);
              if (length != 0 && translated && address[63-length]) expected |= ~64'd0 << (64-length);
              #1;
              assert (guest_policy == {2'(pm),translated} && guest_masked == expected)
                else $fatal(1,"guest PMM modes=%0d access=%0d ctx=%0d translation=%0d mxr=%0d policy=%b expected=%b/%b address=%h/%h",modes,a,ctx,translation,mxrs,guest_policy,2'(pm),translated,guest_masked,expected);
              assert (hstatus_warl == (modes[5:4] == 1 ? 0 : 64'(modes[5:4]) << 48)) else $fatal(1,"HUPMM WARL");
            end
    $display("host/guest pointer masking policy and address sweeps passed");
    $finish;
  end
endmodule
