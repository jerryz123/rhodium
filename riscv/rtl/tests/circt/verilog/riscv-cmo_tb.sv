// Exhaustively checks M/S/U CMO controls, WARL normalization, Sv39 permissions, and PMAs.
// SPDX-License-Identifier: Apache-2.0
module riscv_cmo_tb;
  logic [1:0] operation = 0, access = 0;
  logic user_mode = 0, supervisor_mode = 0, sum = 0, mxr = 0;
  logic [63:0] menvcfg = 0, senvcfg = 0, raw_pte = 0;
  struct packed {
    logic mapped, readable, writable, executable, cacheable, atomic_0, device, read_idempotent, cache_block_zero, instruction_cacheable;
  } physical;
  typedef struct packed { logic permitted; logic [1:0] operation; } permission_t;
  permission_t permission64, permission32, disabled;
  logic [63:0] fields64, fields_management, fields_zero, fields_disabled;
  logic [31:0] fields32;
  logic zero_permitted, zero_disabled, translation_permitted, physical_permitted;
  logic expected_permitted, expected_flush, expected_translation, privilege_ok, access_ok;
  logic [63:0] expected_fields;
  RiscvCmoFixture dut (.*);

  initial begin
    physical = '0;
    for (int privilege = 0; privilege < 3; privilege++) begin
      user_mode = privilege == 0;
      supervisor_mode = privilege == 1;
      for (int m = 0; m < 16; m++) begin
        for (int s = 0; s < 16; s++) begin
          // All non-CMO bits are set, so normalization must exclude them.
          menvcfg = 64'hffffffffffffff0f | (64'(m) << 4);
          senvcfg = 64'hffffffffffffff0f | (64'(s) << 4);
          expected_fields = 64'(m) << 4;
          if ((m & 3) == 2) expected_fields &= ~64'h30;
          for (int op = 0; op < 3; op++) begin
            operation = 2'(op);
            if (op == 0) begin
              expected_permitted = (privilege == 2 || (m & 1) != 0) && (privilege != 0 || (s & 1) != 0);
              expected_flush = (privilege != 2 && (m & 3) == 1) || (privilege == 0 && (s & 3) == 1);
            end else begin
              expected_permitted = (privilege == 2 || (m & 4) != 0) && (privilege != 0 || (s & 4) != 0);
              expected_flush = 0;
            end
            #1;
            assert (permission64.permitted == expected_permitted && permission32 == permission64) else $fatal(1, "CMO privilege mismatch mode=%0d m=%0h s=%0h op=%0d", privilege, m, s, op);
            assert (permission64.operation == 2'(expected_flush ? 2 : op)) else $fatal(1, "invalidate-to-flush policy mismatch");
            assert (!disabled.permitted && !zero_disabled) else $fatal(1, "disabled extension permitted");
            assert (fields64 == expected_fields && fields32 == 32'(expected_fields)) else $fatal(1, "CMO WARL mismatch");
            assert (fields_management == (expected_fields & 64'h70) && fields_zero == (expected_fields & 64'h80) && fields_disabled == 0) else $fatal(1, "CMO extension masks mismatch");
            assert (zero_permitted == ((privilege == 2 || (m & 8) != 0) && (privilege != 0 || (s & 8) != 0))) else $fatal(1, "CBZE mismatch");
          end
        end
      end
      for (int pte = 0; pte < 256; pte++) begin
        raw_pte = 64'(pte);
        for (int options = 0; options < 4; options++) begin
          sum = options[0];
          mxr = options[1];
          for (int kind = 0; kind < 4; kind++) begin
            access = 2'(kind);
            privilege_ok = user_mode ? pte[4] : !pte[4] || (supervisor_mode && sum && kind != 0);
            case (kind)
              0: access_ok = pte[3];
              1: access_ok = pte[1] || (mxr && pte[3]);
              2: access_ok = pte[2] && pte[7];
              3: access_ok = pte[1] || pte[2] || (mxr && pte[3]);
            endcase
            expected_translation = privilege_ok && access_ok && pte[6];
            #1;
            assert (translation_permitted == expected_translation) else $fatal(1, "Sv39 permission mismatch kind=%0d pte=%0h mode=%0d options=%0d", kind, pte, privilege, options);
          end
        end
      end
    end
    // Ignore cacheability, device type, atomic support and CBZE capability.
    for (int attrs = 0; attrs < 512; attrs++) begin
      physical = {9'(attrs), 1'b0};
      #1;
      assert (physical_permitted == (physical.mapped && (physical.readable || physical.writable))) else $fatal(1, "CMO physical permission mismatch");
    end
    $display("CMO privilege, WARL, Sv39 A/D, and physical permissions passed");
    $finish;
  end
endmodule
