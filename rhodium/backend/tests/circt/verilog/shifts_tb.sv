// Simulates fixed-width logical shifts with host and hardware shift amounts.
// SPDX-License-Identifier: Apache-2.0
module shifts_tb;
    logic [7:0] value;
    logic [2:0] amount;
    logic [11:0] wide_amount;
    logic [7:0] left;
    logic [7:0] right;
    logic [7:0] left_wide;
    logic [7:0] right_wide;
    logic [7:0] left_three;
    logic [7:0] right_three;

    ShiftOps8 dut (
        .value(value),
        .amount(amount),
        .wide_amount(wide_amount),
        .left(left),
        .right(right),
        .left_wide(left_wide),
        .right_wide(right_wide),
        .left_three(left_three),
        .right_three(right_three)
    );

    task automatic check_shifts(
        input logic [7:0] value_in,
        input logic [2:0] amount_in,
        input logic [11:0] wide_amount_in,
        input logic [7:0] expected_left,
        input logic [7:0] expected_right,
        input logic [7:0] expected_left_wide,
        input logic [7:0] expected_right_wide
    );
        value = value_in;
        amount = amount_in;
        wide_amount = wide_amount_in;
        #1;
        assert (left == expected_left) else $fatal(1, "narrow-amount left shift failed");
        assert (right == expected_right) else $fatal(1, "narrow-amount right shift failed");
        assert (left_wide == expected_left_wide) else $fatal(1, "wide-amount left shift failed");
        assert (right_wide == expected_right_wide) else $fatal(1, "wide-amount right shift failed");
        assert (left_three == value_in << 3) else $fatal(1, "host-amount left shift failed");
        assert (right_three == value_in >> 3) else $fatal(1, "host-amount right shift failed");
    endtask

    initial begin
        check_shifts(8'h81, 3'd0, 12'd0, 8'h81, 8'h81, 8'h81, 8'h81);
        check_shifts(8'h81, 3'd1, 12'd1, 8'h02, 8'h40, 8'h02, 8'h40);
        check_shifts(8'h81, 3'd3, 12'd7, 8'h08, 8'h10, 8'h80, 8'h01);
        check_shifts(8'hff, 3'd7, 12'd8, 8'h80, 8'h01, 8'h00, 8'h00);
        check_shifts(8'hff, 3'd4, 12'hfff, 8'hf0, 8'h0f, 8'h00, 8'h00);

        $display("logical shift simulation passed");
        $finish;
    end
endmodule
