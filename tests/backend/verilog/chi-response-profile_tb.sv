// Checks every response opcode against independent milestone expectations.
// SPDX-License-Identifier: Apache-2.0
module chi_response_profile_tb;
  logic [4:0] opcode;
  wire [1:0] effects, legacy_effects;
  wire dbid, completion, legacy_dbid, retry_only;
  CHIResponseProfileFixture dut(.*);
  initial begin
    for (int op = 0; op < 32; op++) begin
      logic [1:0] expected;
      opcode = 5'(op);
      case (op)
        'h6, 'he: expected = 2'b01;
        'h4: expected = 2'b10;
        'h5: expected = 2'b11;
        default: expected = 2'b00;
      endcase
      #1;
      assert (effects === expected && legacy_effects === expected &&
              dbid === expected[0] && legacy_dbid === expected[0] &&
              completion === expected[1] && retry_only === 1'b0)
        else $fatal(1, "profile decode for opcode %0h", op);
    end
    $display("CHI response profiles passed: all 32 opcodes");
    $finish;
  end
endmodule
