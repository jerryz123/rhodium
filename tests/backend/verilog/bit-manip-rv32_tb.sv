// Exercises standard-B and Zicond resource controls in the reusable RV32 ALU.
// SPDX-License-Identifier: Apache-2.0
module bit_manip_rv32_tb;
  logic [31:0] left;
  logic [31:0] right;
  AluControl control;
  logic [31:0] result;

  ALU dut (.*);

  task automatic check_result(input logic [2:0] result_select, subselect, input logic flag_a, flag_b, input logic [31:0] left_value, right_value, expected);
    control = '0;
    control.result_select = result_select;
    case (result_select)
      3'd0: begin control.shift_add_amount = subselect[1:0]; end
      3'd1: begin control.shift_right = flag_a; control.extract_bit = flag_b; end
      3'd2: begin control.logic_select = subselect[1:0]; control.invert_right = flag_a; control.logic_right_select = flag_b ? 2'd1 : 2'd0; end
      3'd4: begin control.subtract = 1'b1; control.signed_compare = flag_a; control.maximum = flag_b; end
      3'd5: begin control.count_select = subselect[1:0]; end
      3'd6: begin control.shift_right = flag_a; end
      3'd7: begin control.unary_select = subselect; end
      default: begin end
    endcase
    left = left_value;
    right = right_value;
    #1;
    assert (result === expected) else $fatal(1, "RV32 bit-manip result %h, expected %h", result, expected);
  endtask

  task automatic check_conditional(input logic invert_right, input logic [31:0] left_value, right_value, expected);
    control = '0;
    control.result_select = 3'd2;
    control.logic_select = 2'd0;
    control.invert_right = invert_right;
    control.logic_right_select = 2'd2;
    left = left_value;
    right = right_value;
    #1;
    assert (result === expected) else $fatal(1, "RV32 conditional result %h, expected %h", result, expected);
  endtask

  initial begin
    check_result(0, 3, 0, 0, 32'd3, 32'd5, 32'd29);
    check_result(2, 0, 1, 0, 32'hff00, 32'h0f0f, 32'hf000);
    check_result(2, 1, 1, 0, 32'hf0, 32'hff, 32'hffff_fff0);
    check_result(2, 2, 1, 0, 32'h55, 32'haa, 32'hffff_ff00);
    check_result(5, 0, 0, 0, 32'h10, 0, 32'd27);
    check_result(5, 1, 0, 0, 32'h100, 0, 32'd8);
    check_result(5, 2, 0, 0, 32'hf0f, 0, 32'd8);
    check_result(4, 0, 1, 0, -32'd1, 32'd1, -32'd1);
    check_result(4, 0, 0, 1, -32'd1, 32'd1, -32'd1);
    check_result(7, 0, 0, 0, 32'h0100_0200, 0, 32'hff00_ff00);
    check_result(7, 1, 0, 0, 32'h0123_4567, 0, 32'h6745_2301);
    check_result(6, 0, 0, 0, 32'h8000_0001, 1, 32'h0000_0003);
    check_result(6, 0, 1, 0, 32'h8000_0001, 1, 32'hc000_0000);
    check_result(7, 2, 0, 0, 32'h80, 0, 32'hffff_ff80);
    check_result(7, 3, 0, 0, 32'h8001, 0, 32'hffff_8001);
    check_result(7, 4, 0, 0, 32'hffff_8001, 0, 32'h0000_8001);
    check_result(2, 0, 1, 1, 32'hff, 3, 32'hf7);
    check_result(1, 0, 1, 1, 32'h08, 3, 32'h1);
    check_result(2, 2, 0, 1, 32'h08, 3, 32'h0);
    check_result(2, 1, 0, 1, 32'h00, 31, 32'h8000_0000);
    check_conditional(0, 32'h89ab_cdef, 0, 0);
    check_conditional(0, 32'h89ab_cdef, 1, 32'h89ab_cdef);
    check_conditional(1, 32'h89ab_cdef, 0, 32'h89ab_cdef);
    check_conditional(1, 32'h89ab_cdef, 1, 0);
    $display("RV32 bit manipulation simulation passed");
    $finish;
  end
endmodule
