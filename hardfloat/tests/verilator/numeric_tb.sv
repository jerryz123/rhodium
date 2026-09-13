// Exhaustively checks binary16 minimum/maximum, integral rounding, and modulo conversion behavior.
// SPDX-License-Identifier: BSD-3-Clause
module hardfloat_numeric_tb;
  integer value_index;
  integer operation_index;
  integer mode_index;
  logic [15:0] a;
  logic [15:0] b;
  logic [63:0] wide_value;
  logic [1:0] min_max_operation;
  logic [2:0] rounding_mode;
  logic raise_inexact;
  logic signed_output;
  logic [15:0] min_max;
  logic [4:0] min_max_flags;
  logic [15:0] integral;
  logic [4:0] integral_flags;
  logic [7:0] modulo_integer;
  logic [2:0] modulo_flags;
  logic [7:0] standard_integer;
  logic [2:0] standard_flags;
  logic [31:0] wide_modulo_integer;
  logic [2:0] wide_modulo_flags;
  logic [63:0] wide_integral;
  logic [4:0] wide_integral_flags;

  HardFloatNumeric dut (
    .a(a),
    .b(b),
    .wide_value(wide_value),
    .min_max_operation(min_max_operation),
    .rounding_mode(rounding_mode),
    .raise_inexact(raise_inexact),
    .signed_output(signed_output),
    .min_max(min_max),
    .min_max_flags(min_max_flags),
    .integral(integral),
    .integral_flags(integral_flags),
    .modulo_integer(modulo_integer),
    .modulo_flags(modulo_flags),
    .standard_integer(standard_integer),
    .standard_flags(standard_flags),
    .wide_modulo_integer(wide_modulo_integer),
    .wide_modulo_flags(wide_modulo_flags),
    .wide_integral(wide_integral),
    .wide_integral_flags(wide_integral_flags)
  );

  function automatic logic f16_nan(input logic [15:0] value);
    f16_nan = (value[14:10] == 5'h1f) && (value[9:0] != 10'h000);
  endfunction

  function automatic logic f16_signaling_nan(input logic [15:0] value);
    f16_signaling_nan = f16_nan(value) && !value[9];
  endfunction

  function automatic logic f16_less(input logic [15:0] left, input logic [15:0] right);
    logic [14:0] left_magnitude;
    logic [14:0] right_magnitude;
    begin
      left_magnitude = left[14:0];
      right_magnitude = right[14:0];
      if ((left_magnitude == 0) && (right_magnitude == 0))
        f16_less = 1'b0;
      else if (left[15] != right[15])
        f16_less = left[15];
      else if (left[15])
        f16_less = left_magnitude > right_magnitude;
      else
        f16_less = left_magnitude < right_magnitude;
    end
  endfunction

  function automatic logic [15:0] expected_min_max(
    input logic [15:0] left,
    input logic [15:0] right,
    input logic [1:0] operation
  );
    logic left_nan;
    logic right_nan;
    logic maximum;
    logic prefer_number;
    logic choose_left;
    begin
      left_nan = f16_nan(left);
      right_nan = f16_nan(right);
      maximum = operation[0];
      prefer_number = operation[1];
      if ((left_nan && right_nan) || ((left_nan || right_nan) && !prefer_number))
        expected_min_max = 16'h7e00;
      else if (left_nan)
        expected_min_max = right;
      else if (right_nan)
        expected_min_max = left;
      else if ((left[14:0] == 0) && (right[14:0] == 0))
        expected_min_max = {maximum ? (left[15] && right[15]) : (left[15] || right[15]), 15'h0000};
      else begin
        choose_left = maximum ? f16_less(right, left) : f16_less(left, right);
        expected_min_max = choose_left ? left : right;
      end
    end
  endfunction

  function automatic logic [2:0] selected_mode(input integer index);
    begin
      case (index)
        0: selected_mode = 3'b000;
        1: selected_mode = 3'b001;
        2: selected_mode = 3'b010;
        3: selected_mode = 3'b011;
        4: selected_mode = 3'b100;
        default: selected_mode = 3'b110;
      endcase
    end
  endfunction

  function automatic logic [15:0] expected_integral(input logic [15:0] value, input logic [2:0] mode);
    logic [4:0] exponent;
    integer fractional_bits;
    logic [15:0] mask;
    logic [15:0] magnitude;
    logic [15:0] truncated;
    logic guard;
    logic sticky;
    logic retained_lsb;
    logic inexact;
    logic increment;
    begin
      magnitude = {1'b0, value[14:0]};
      exponent = value[14:10];
      if (f16_nan(value))
        expected_integral = 16'h7e00;
      else if ((exponent == 31) || (magnitude == 0) || (exponent >= 25))
        expected_integral = value;
      else if (exponent < 15) begin
        inexact = magnitude != 0;
        case (mode)
          3'b000: increment = magnitude > 16'h3800;
          3'b001: increment = 1'b0;
          3'b010: increment = value[15] && inexact;
          3'b011: increment = !value[15] && inexact;
          3'b100: increment = magnitude >= 16'h3800;
          3'b110: increment = inexact;
          default: increment = 1'b0;
        endcase
        expected_integral = {value[15], increment ? 15'h3c00 : 15'h0000};
      end else begin
        fractional_bits = 25 - {27'b0, exponent};
        mask = 16'hffff >> (16 - fractional_bits);
        truncated = magnitude & ~mask;
        guard = magnitude[fractional_bits - 1];
        sticky = (magnitude & (16'hffff >> (17 - fractional_bits))) != 0;
        retained_lsb = magnitude[fractional_bits];
        inexact = guard || sticky;
        increment = ((mode == 3'b000) && guard && (sticky || retained_lsb)) ||
                    ((mode == 3'b100) && guard) ||
                    ((mode == 3'b010) && value[15] && inexact) ||
                    ((mode == 3'b011) && !value[15] && inexact);
        if ((mode == 3'b110) && inexact)
          truncated = truncated | (16'h0001 << fractional_bits);
        else if (increment)
          truncated = truncated + (16'h0001 << fractional_bits);
        expected_integral = {value[15], truncated[14:0]};
      end
    end
  endfunction

  function automatic logic expected_integral_inexact(input logic [15:0] value);
    logic [4:0] exponent;
    integer fractional_bits;
    logic [14:0] mask;
    begin
      exponent = value[14:10];
      if ((exponent == 31) || (value[14:0] == 0) || (exponent >= 25))
        expected_integral_inexact = 1'b0;
      else if (exponent < 15)
        expected_integral_inexact = 1'b1;
      else begin
        fractional_bits = 25 - {27'b0, exponent};
        mask = 15'h7fff >> (15 - fractional_bits);
        expected_integral_inexact = (value[14:0] & mask) != 0;
      end
    end
  endfunction

  function automatic logic [7:0] expected_modulo(input logic [15:0] value, input logic [2:0] mode);
    integer exponent;
    integer significand;
    integer shift;
    integer magnitude;
    integer signed_result;
    logic guard;
    logic sticky;
    logic inexact;
    logic increment;
    begin
      if (value[14:10] == 31)
        expected_modulo = 8'h00;
      else begin
        if (value[14:10] == 0) begin
          exponent = -14;
          significand = {22'b0, value[9:0]};
        end else begin
          exponent = {27'b0, value[14:10]} - 15;
          significand = 1024 + {22'b0, value[9:0]};
        end
        shift = 10 - exponent;
        if (shift <= 0) begin
          magnitude = significand << -shift;
          guard = 1'b0;
          sticky = 1'b0;
        end else begin
          magnitude = significand >> shift;
          guard = significand[shift - 1];
          sticky = (significand & ((1 << (shift - 1)) - 1)) != 0;
        end
        inexact = guard || sticky;
        increment = ((mode == 3'b000) && guard && (sticky || magnitude[0])) ||
                    ((mode == 3'b100) && guard) ||
                    ((mode == 3'b010) && value[15] && inexact) ||
                    ((mode == 3'b011) && !value[15] && inexact);
        if ((mode == 3'b110) && inexact)
          magnitude = magnitude | 1;
        else if (increment)
          magnitude = magnitude + 1;
        signed_result = value[15] ? -magnitude : magnitude;
        expected_modulo = signed_result[7:0];
      end
    end
  endfunction

  task automatic check_min_max_range(input logic [15:0] right);
    begin
      b = right;
      for (operation_index = 0; operation_index < 4; operation_index = operation_index + 1) begin
        min_max_operation = operation_index[1:0];
        for (value_index = 0; value_index < 65536; value_index = value_index + 1) begin
          a = value_index[15:0];
          #1;
          if (min_max !== expected_min_max(a, b, min_max_operation))
            $fatal(1, "min/max mismatch: a=%h b=%h operation=%h output=%h expected=%h", a, b, min_max_operation, min_max, expected_min_max(a, b, min_max_operation));
          if (min_max_flags !== {f16_signaling_nan(a) || f16_signaling_nan(b), 4'b0000})
            $fatal(1, "min/max flags mismatch: a=%h b=%h operation=%h flags=%h", a, b, min_max_operation, min_max_flags);
        end
      end
    end
  endtask

  task automatic check_wide_integral(
    input logic [63:0] value,
    input logic [2:0] mode,
    input logic report_inexact,
    input logic [63:0] expected_value,
    input logic [4:0] expected_flags
  );
    begin
      wide_value = value;
      rounding_mode = mode;
      raise_inexact = report_inexact;
      #1;
      if ((wide_integral !== expected_value) || (wide_integral_flags !== expected_flags))
        $fatal(1, "binary64 integral mismatch: input=%h mode=%h report_inexact=%b output=%h flags=%h expected=%h/%h", value, mode, report_inexact, wide_integral, wide_integral_flags, expected_value, expected_flags);
    end
  endtask

  task automatic check_wide_modulo(
    input logic [63:0] value,
    input logic [2:0] mode,
    input logic signed_mode,
    input logic [31:0] expected_integer,
    input logic [2:0] expected_flags
  );
    begin
      wide_value = value;
      rounding_mode = mode;
      signed_output = signed_mode;
      #1;
      if ((wide_modulo_integer !== expected_integer) || (wide_modulo_flags !== expected_flags))
        $fatal(1, "binary64 modulo mismatch: input=%h mode=%h signed=%b output=%h flags=%h expected=%h/%h", value, mode, signed_mode, wide_modulo_integer, wide_modulo_flags, expected_integer, expected_flags);
    end
  endtask

  initial begin
    a = 16'h0000;
    b = 16'h0000;
    min_max_operation = 2'b00;
    rounding_mode = 3'b000;
    raise_inexact = 1'b1;
    signed_output = 1'b1;
    wide_value = 64'h0000000000000000;

    check_min_max_range(16'h3c00);
    check_min_max_range(16'h0000);
    check_min_max_range(16'h8000);
    check_min_max_range(16'h7e00);
    check_min_max_range(16'h7d00);

    check_wide_modulo(64'h3ff8000000000000, 3'b001, 1'b1, 32'h00000001, 3'b001);
    check_wide_modulo(64'hbff8000000000000, 3'b001, 1'b1, 32'hffffffff, 3'b001);
    check_wide_modulo(64'h41f0000000000000, 3'b001, 1'b1, 32'h00000000, 3'b010);
    check_wide_modulo(64'h41f0000000100000, 3'b001, 1'b1, 32'h00000001, 3'b010);
    check_wide_modulo(64'h4520000000000001, 3'b001, 1'b1, 32'h80000000, 3'b010);
    check_wide_modulo(64'h4530000000000000, 3'b001, 1'b1, 32'h00000000, 3'b010);
    check_wide_modulo(64'h7fefffffffffffff, 3'b001, 1'b1, 32'h00000000, 3'b010);
    check_wide_modulo(64'h7ff0000000000000, 3'b001, 1'b1, 32'h00000000, 3'b100);
    check_wide_modulo(64'h7ff8000000000000, 3'b001, 1'b1, 32'h00000000, 3'b100);
    check_wide_modulo(64'h41e0000000000000, 3'b001, 1'b0, 32'h80000000, 3'b000);
    check_wide_modulo(64'hbff0000000000000, 3'b001, 1'b0, 32'hffffffff, 3'b010);

    check_wide_integral(64'h3ff8000000000000, 3'b000, 1'b1, 64'h4000000000000000, 5'b00001);
    check_wide_integral(64'h4004000000000000, 3'b000, 1'b1, 64'h4000000000000000, 5'b00001);
    check_wide_integral(64'h4004000000000000, 3'b100, 1'b1, 64'h4008000000000000, 5'b00001);
    check_wide_integral(64'hbfd3333333333333, 3'b010, 1'b1, 64'hbff0000000000000, 5'b00001);
    check_wide_integral(64'hbfd3333333333333, 3'b011, 1'b1, 64'h8000000000000000, 5'b00001);
    check_wide_integral(64'h43b0000000000000, 3'b000, 1'b1, 64'h43b0000000000000, 5'b00000);
    check_wide_integral(64'h7ff8000000000001, 3'b000, 1'b1, 64'h7ff8000000000000, 5'b00000);
    check_wide_integral(64'h7ff0000000000001, 3'b000, 1'b1, 64'h7ff8000000000000, 5'b10000);
    check_wide_integral(64'h3ff8000000000000, 3'b000, 1'b0, 64'h4000000000000000, 5'b00000);

    raise_inexact = 1'b1;
    signed_output = 1'b1;
    for (mode_index = 0; mode_index < 6; mode_index = mode_index + 1) begin
      rounding_mode = selected_mode(mode_index);
      for (value_index = 0; value_index < 65536; value_index = value_index + 1) begin
        a = value_index[15:0];
        #1;
        if (integral !== expected_integral(a, rounding_mode))
          $fatal(1, "integral mismatch: input=%h mode=%h output=%h expected=%h", a, rounding_mode, integral, expected_integral(a, rounding_mode));
        if (integral_flags !== {f16_signaling_nan(a), 3'b000, expected_integral_inexact(a)})
          $fatal(1, "integral flags mismatch: input=%h mode=%h flags=%h", a, rounding_mode, integral_flags);
        if (modulo_integer !== expected_modulo(a, rounding_mode))
          $fatal(1, "modulo mismatch: input=%h mode=%h output=%h expected=%h", a, rounding_mode, modulo_integer, expected_modulo(a, rounding_mode));
        if (modulo_flags !== standard_flags)
          $fatal(1, "modulo flags differ from standard conversion: input=%h mode=%h modulo=%h standard=%h", a, rounding_mode, modulo_flags, standard_flags);
        if ((standard_flags[2:1] == 2'b00) && (modulo_integer !== standard_integer))
          $fatal(1, "in-range modulo differs from standard conversion: input=%h mode=%h modulo=%h standard=%h", a, rounding_mode, modulo_integer, standard_integer);
      end
    end

    raise_inexact = 1'b0;
    rounding_mode = 3'b000;
    for (value_index = 0; value_index < 65536; value_index = value_index + 1) begin
      a = value_index[15:0];
      #1;
      if (integral_flags !== {f16_signaling_nan(a), 4'b0000})
        $fatal(1, "suppressed integral flags mismatch: input=%h flags=%h", a, integral_flags);
    end

    signed_output = 1'b0;
    for (value_index = 0; value_index < 65536; value_index = value_index + 1) begin
      a = value_index[15:0];
      #1;
      if (modulo_flags !== standard_flags)
        $fatal(1, "unsigned modulo flags differ from standard conversion: input=%h modulo=%h standard=%h", a, modulo_flags, standard_flags);
    end

    $display("HardFloat numeric extension simulation passed");
    $finish;
  end
endmodule
