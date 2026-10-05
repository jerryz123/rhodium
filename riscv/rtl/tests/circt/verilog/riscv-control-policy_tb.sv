// SPDX-License-Identifier: Apache-2.0
module riscv_control_policy_tb;
  logic [63:0] pending = 0, enabled = 0, delegated = 0, mstatus = 0;
  logic [1:0] privilege = 0, target32, target64;
  logic [31:0] vtype = 0;
  logic valid32, valid64, vill32, vill64;
  logic [3:0] cause32, cause64;
  logic [7:0] vlmax32, vlmax64;
  RiscvControlPolicyFixture dut (.*);
  int codes[7] = '{11, 3, 7, 9, 1, 5, 13};
  int checks = 0;

  task automatic check_interrupt;
    bit found, global_enable;
    int expected_cause, expected_target;
    found = 0; expected_cause = 0; expected_target = 0;
    // First search M-targeted causes, then S-targeted causes, each in cause order.
    for (int dest = 0; dest < 2; dest++) begin
      global_enable = dest == 0 ? (privilege != 3 || mstatus[3]) :
                                 (privilege == 0 || (privilege == 1 && mstatus[1]));
      for (int index = 0; index < 7; index++) begin
        if (!found && pending[codes[index]] && enabled[codes[index]] &&
            delegated[codes[index]] == 1'(dest) && global_enable) begin
          found = 1; expected_cause = codes[index]; expected_target = dest;
        end
      end
    end
    #1;
    assert (valid32 == found && valid64 == found) else $fatal(1, "interrupt validity");
    if (found) begin
      assert (cause32 == 4'(expected_cause) && cause64 == 4'(expected_cause) &&
              target32 == 2'(expected_target) && target64 == 2'(expected_target))
        else $fatal(1, "interrupt priority p=%h e=%h d=%h priv=%d status=%h got=%d/%d expected=%d/%d",
                    pending, enabled, delegated, privilege, mstatus, cause32, target32, expected_cause, expected_target);
    end
    checks++;
  endtask

  initial begin
    // Sweep cause pairs, delegation, per-cause enable, privilege, and global gates.
    for (int first = 0; first < 7; first++) begin
      for (int second = first; second < 7; second++) begin
        pending = (64'd1 << codes[first]) | (64'd1 << codes[second]);
        for (int route = 0; route < 4; route++) begin
          delegated = (route[0] ? 64'd1 << codes[first] : 0) | (route[1] ? 64'd1 << codes[second] : 0);
          for (int mask = 0; mask < 4; mask++) begin
            enabled = (mask[0] ? 64'd1 << codes[first] : 0) | (mask[1] ? 64'd1 << codes[second] : 0);
            for (int mode = 0; mode < 3; mode++) begin
              privilege = mode == 2 ? 2'd3 : 2'(mode);
              for (int gates = 0; gates < 4; gates++) begin
                mstatus = (gates[0] ? 64'd8 : 0) | (gates[1] ? 64'd2 : 0);
                check_interrupt();
              end
            end
          end
        end
      end
    end
    pending = '1; enabled = '1; delegated = 0; privilege = 0; check_interrupt();
    pending = 64'd1 << 63; check_interrupt(); // Unsupported bits cannot become a cause.
    for (int raw = 0; raw < 256; raw++) begin
      int sew, lm, maximum32, maximum64;
      bit reserved;
      vtype = 32'(raw); sew = (raw >> 3) & 7; lm = raw & 7;
      reserved = lm == 4 || sew > 3;
      if (lm >= 4) lm -= 8;
      maximum32 = reserved || sew > 2 || sew > lm + 2 ? 0 :
                  (lm >= 0 ? (128 / (8 << sew)) << lm : (128 / (8 << sew)) >> -lm);
      maximum64 = reserved || sew > lm + 3 ? 0 :
                  (lm >= 0 ? (128 / (8 << sew)) << lm : (128 / (8 << sew)) >> -lm);
      #1;
      assert (vill32 == (maximum32 == 0) && vlmax32 == 8'(maximum32) &&
              vill64 == (maximum64 == 0) && vlmax64 == 8'(maximum64))
        else $fatal(1, "ELEN vtype=%h max32=%d max64=%d", vtype, vlmax32, vlmax64);
      checks++;
    end
    $display("RISC-V destination priority and ELEN policy passed: %0d checks", checks);
    $finish;
  end
endmodule
