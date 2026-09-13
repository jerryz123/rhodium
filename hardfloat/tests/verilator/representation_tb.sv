// Checks permanent HardFloat representation, conversion, rounding, and arithmetic behavior.
// SPDX-License-Identifier: BSD-3-Clause
module hardfloat_representation_tb;
  integer ieee_value;
  integer low_mask_input;
  logic [15:0] a;
  logic [15:0] b;
  logic signaling;
  logic invalid_exception;
  logic infinite_exception;
  logic [2:0] rounding_mode;
  logic tininess_mode;
  logic [1:0] rounding_extra;
  logic [15:0] integer_value;
  logic integer_signed;
  logic integer_output_signed;
  logic [31:0] wide_ieee;
  logic add_subtract;
  logic [15:0] fma_c;
  logic [1:0] fma_operation;
  logic [16:0] recoded_a;
  logic [15:0] restored_a;
  logic [9:0] classification;
  logic [3:0] leading_zero_count;
  logic [11:0] descending_low_mask;
  logic [11:0] ascending_low_mask;
  logic lt;
  logic eq;
  logic gt;
  logic [4:0] exception_flags;
  logic [16:0] rounded_recoded;
  logic [15:0] rounded_ieee;
  logic [4:0] rounding_flags;
  logic [15:0] integer_as_ieee;
  logic [4:0] integer_conversion_flags;
  logic [15:0] ieee_as_integer;
  logic [2:0] integer_result_flags;
  logic [31:0] widened_ieee;
  logic [4:0] widened_flags;
  logic [15:0] narrowed_ieee;
  logic [4:0] narrowed_flags;
  logic [15:0] product_ieee;
  logic [4:0] product_flags;
  logic [15:0] sum_ieee;
  logic [4:0] sum_flags;
  logic [15:0] fma_ieee;
  logic [4:0] fma_flags;

  HardFloatRepresentation dut (
    .a(a),
    .b(b),
    .signaling(signaling),
    .invalid_exception(invalid_exception),
    .infinite_exception(infinite_exception),
    .rounding_mode(rounding_mode),
    .tininess_mode(tininess_mode),
    .rounding_extra(rounding_extra),
    .integer_value(integer_value),
    .integer_signed(integer_signed),
    .integer_output_signed(integer_output_signed),
    .wide_ieee(wide_ieee),
    .add_subtract(add_subtract),
    .fma_c(fma_c),
    .fma_operation(fma_operation),
    .recoded_a(recoded_a),
    .restored_a(restored_a),
    .classification(classification),
    .leading_zero_count(leading_zero_count),
    .descending_low_mask(descending_low_mask),
    .ascending_low_mask(ascending_low_mask),
    .lt(lt),
    .eq(eq),
    .gt(gt),
    .exception_flags(exception_flags),
    .rounded_recoded(rounded_recoded),
    .rounded_ieee(rounded_ieee),
    .rounding_flags(rounding_flags),
    .integer_as_ieee(integer_as_ieee),
    .integer_conversion_flags(integer_conversion_flags),
    .ieee_as_integer(ieee_as_integer),
    .integer_result_flags(integer_result_flags),
    .widened_ieee(widened_ieee),
    .widened_flags(widened_flags),
    .narrowed_ieee(narrowed_ieee),
    .narrowed_flags(narrowed_flags),
    .product_ieee(product_ieee),
    .product_flags(product_flags),
    .sum_ieee(sum_ieee),
    .sum_flags(sum_flags),
    .fma_ieee(fma_ieee),
    .fma_flags(fma_flags)
  );

  task automatic check_case(
    input logic [15:0] next_a,
    input logic [15:0] next_b,
    input logic next_signaling,
    input logic [9:0] expected_class,
    input logic expected_lt,
    input logic expected_eq,
    input logic expected_gt,
    input logic expected_invalid
  );
    begin
      a = next_a;
      b = next_b;
      signaling = next_signaling;
      #1;
      if (restored_a !== next_a)
        $fatal(1, "F16 round trip failed: input=%h output=%h", next_a, restored_a);
      if (classification !== expected_class)
        $fatal(1, "classification failed: input=%h class=%h expected=%h", next_a, classification, expected_class);
      if ({lt, eq, gt} !== {expected_lt, expected_eq, expected_gt})
        $fatal(1, "comparison failed: a=%h b=%h got=%b%b%b", next_a, next_b, lt, eq, gt);
      if (exception_flags !== {expected_invalid, 4'b0000})
        $fatal(1, "exception flags failed: got=%h", exception_flags);
    end
  endtask

  task automatic check_fma(
    input logic [15:0] next_a,
    input logic [15:0] next_b,
    input logic [15:0] next_c,
    input logic [1:0] next_operation,
    input logic [2:0] next_mode,
    input logic [15:0] expected_ieee,
    input logic [4:0] expected_flags
  );
    begin
      a = next_a;
      b = next_b;
      fma_c = next_c;
      fma_operation = next_operation;
      rounding_mode = next_mode;
      #1;
      if ((fma_ieee !== expected_ieee) || (fma_flags !== expected_flags))
        $fatal(1, "fused multiply-add failed: a=%h b=%h c=%h operation=%h mode=%h output=%h flags=%h expected=%h/%h",
               next_a, next_b, next_c, next_operation, next_mode, fma_ieee, fma_flags, expected_ieee, expected_flags);
    end
  endtask

  task automatic check_multiply(
    input logic [15:0] next_a,
    input logic [15:0] next_b,
    input logic [2:0] next_mode,
    input logic [15:0] expected_ieee,
    input logic [4:0] expected_flags
  );
    begin
      a = next_a;
      b = next_b;
      rounding_mode = next_mode;
      #1;
      if ((product_ieee !== expected_ieee) || (product_flags !== expected_flags))
        $fatal(1, "multiply failed: a=%h b=%h mode=%h output=%h flags=%h expected=%h/%h",
               next_a, next_b, next_mode, product_ieee, product_flags, expected_ieee, expected_flags);
    end
  endtask

  task automatic check_add(
    input logic [15:0] next_a,
    input logic [15:0] next_b,
    input logic next_subtract,
    input logic [2:0] next_mode,
    input logic [15:0] expected_ieee,
    input logic [4:0] expected_flags
  );
    begin
      a = next_a;
      b = next_b;
      add_subtract = next_subtract;
      rounding_mode = next_mode;
      #1;
      if ((sum_ieee !== expected_ieee) || (sum_flags !== expected_flags))
        $fatal(1, "add failed: a=%h b=%h subtract=%b mode=%h output=%h flags=%h expected=%h/%h",
               next_a, next_b, next_subtract, next_mode, sum_ieee, sum_flags, expected_ieee, expected_flags);
    end
  endtask

  task automatic check_integer_to_float(
    input logic [15:0] next_value,
    input logic next_signed,
    input logic [2:0] next_mode,
    input logic [15:0] expected_ieee,
    input logic [4:0] expected_flags
  );
    begin
      integer_value = next_value;
      integer_signed = next_signed;
      rounding_mode = next_mode;
      #1;
      if ((integer_as_ieee !== expected_ieee) || (integer_conversion_flags !== expected_flags))
        $fatal(1, "integer-to-float failed: input=%h signed=%b mode=%h output=%h flags=%h expected=%h/%h",
               next_value, next_signed, next_mode, integer_as_ieee, integer_conversion_flags, expected_ieee, expected_flags);
    end
  endtask

  task automatic check_float_to_integer(
    input logic [15:0] next_ieee,
    input logic next_signed,
    input logic [2:0] next_mode,
    input logic [15:0] expected_integer,
    input logic [2:0] expected_flags
  );
    begin
      a = next_ieee;
      integer_output_signed = next_signed;
      rounding_mode = next_mode;
      #1;
      if ((ieee_as_integer !== expected_integer) || (integer_result_flags !== expected_flags))
        $fatal(1, "float-to-integer failed: input=%h signed=%b mode=%h output=%h flags=%h expected=%h/%h",
               next_ieee, next_signed, next_mode, ieee_as_integer, integer_result_flags, expected_integer, expected_flags);
    end
  endtask

  task automatic check_narrow(
    input logic [31:0] next_ieee,
    input logic [2:0] next_mode,
    input logic [15:0] expected_ieee,
    input logic [4:0] expected_flags
  );
    begin
      wide_ieee = next_ieee;
      rounding_mode = next_mode;
      #1;
      if ((narrowed_ieee !== expected_ieee) || (narrowed_flags !== expected_flags))
        $fatal(1, "RecFN narrowing failed: input=%h mode=%h output=%h flags=%h expected=%h/%h",
               next_ieee, next_mode, narrowed_ieee, narrowed_flags, expected_ieee, expected_flags);
    end
  endtask

  function automatic logic [11:0] low_ones(input integer count);
    begin
      if (count <= 0)
        low_ones = 12'h000;
      else if (count >= 12)
        low_ones = 12'hfff;
      else
        low_ones = (12'h001 << count) - 1'b1;
    end
  endfunction

  task automatic check_round(
    input logic [15:0] next_a,
    input logic [1:0] next_extra,
    input logic [2:0] next_mode,
    input logic [15:0] expected_ieee,
    input logic [4:0] expected_flags
  );
    begin
      a = next_a;
      rounding_extra = next_extra;
      rounding_mode = next_mode;
      #1;
      if ((rounded_ieee !== expected_ieee) || (rounding_flags !== expected_flags))
        $fatal(1, "rounding failed: input=%h extra=%h mode=%h output=%h flags=%h expected=%h/%h",
               next_a, next_extra, next_mode, rounded_ieee, rounding_flags, expected_ieee, expected_flags);
    end
  endtask

  initial begin
    invalid_exception = 1'b0;
    infinite_exception = 1'b0;
    rounding_mode = 3'b000;
    tininess_mode = 1'b1;
    rounding_extra = 2'b00;
    integer_value = 16'h0000;
    integer_signed = 1'b0;
    integer_output_signed = 1'b0;
    wide_ieee = 32'h00000000;
    add_subtract = 1'b0;
    fma_c = 16'h0000;
    fma_operation = 2'b00;
    check_case(16'h0000, 16'h8000, 1'b0, 10'b0000010000, 1'b0, 1'b1, 1'b0, 1'b0);
    check_case(16'h8000, 16'h3c00, 1'b0, 10'b0000001000, 1'b1, 1'b0, 1'b0, 1'b0);
    check_case(16'h3c00, 16'h4000, 1'b0, 10'b0001000000, 1'b1, 1'b0, 1'b0, 1'b0);
    check_case(16'h7c00, 16'hfc00, 1'b0, 10'b0010000000, 1'b0, 1'b0, 1'b1, 1'b0);
    check_case(16'h7e00, 16'h3c00, 1'b0, 10'b1000000000, 1'b0, 1'b0, 1'b0, 1'b0);
    check_case(16'h7d00, 16'h3c00, 1'b0, 10'b0100000000, 1'b0, 1'b0, 1'b0, 1'b1);
    check_case(16'h7e00, 16'h3c00, 1'b1, 10'b1000000000, 1'b0, 1'b0, 1'b0, 1'b1);

    b = 16'h0000;
    add_subtract = 1'b0;
    for (ieee_value = 0; ieee_value < 65536; ieee_value = ieee_value + 1) begin
      a = ieee_value[15:0];
      low_mask_input = {26'b0, a[5:0]};
      #1;
      if (restored_a !== a)
        $fatal(1, "exhaustive F16 round trip failed: input=%h output=%h", a, restored_a);
      if ((a[14:10] == 5'h1f) && (a[9:0] != 10'h000)) begin
        if (rounded_ieee !== 16'h7e00)
          $fatal(1, "NaN canonicalization failed: input=%h output=%h", a, rounded_ieee);
      end else if (rounded_ieee !== a) begin
        $fatal(1, "resize and rounding identity failed: input=%h output=%h", a, rounded_ieee);
      end
      if (rounding_flags !== 5'b00000)
        $fatal(1, "unexpected identity rounding flags: input=%h flags=%h", a, rounding_flags);
      if (descending_low_mask !== low_ones(18 - low_mask_input))
        $fatal(1, "descending low mask failed: input=%h mask=%h", a[5:0], descending_low_mask);
      if (ascending_low_mask !== low_ones(low_mask_input - 6))
        $fatal(1, "ascending low mask failed: input=%h mask=%h", a[5:0], ascending_low_mask);
      if ((a[14:10] == 5'h1f) && (a[9:0] != 10'h000)) begin
        if ((sum_ieee !== 16'h7e00) || (sum_flags !== {a[9] == 1'b0, 4'b0000}))
          $fatal(1, "exhaustive add-zero NaN failed: input=%h output=%h flags=%h", a, sum_ieee, sum_flags);
      end else if (a == 16'h8000) begin
        if ((sum_ieee !== 16'h0000) || (sum_flags !== 5'b00000))
          $fatal(1, "negative-zero plus zero failed: output=%h flags=%h", sum_ieee, sum_flags);
      end else if ((sum_ieee !== a) || (sum_flags !== 5'b00000)) begin
        $fatal(1, "exhaustive add-zero identity failed: input=%h output=%h flags=%h", a, sum_ieee, sum_flags);
      end
    end

    add_subtract = 1'b1;
    for (ieee_value = 0; ieee_value < 65536; ieee_value = ieee_value + 1) begin
      a = ieee_value[15:0];
      b = ieee_value[15:0];
      #1;
      if (a[14:10] == 5'h1f) begin
        if ((sum_ieee !== 16'h7e00) || (sum_flags !== {(a[9:0] == 10'h000) || (a[9] == 1'b0), 4'b0000}))
          $fatal(1, "exhaustive subtract-self special case failed: input=%h output=%h flags=%h", a, sum_ieee, sum_flags);
      end else if ((sum_ieee !== 16'h0000) || (sum_flags !== 5'b00000)) begin
        $fatal(1, "exhaustive subtract-self cancellation failed: input=%h output=%h flags=%h", a, sum_ieee, sum_flags);
      end
    end
    add_subtract = 1'b0;

    a = 16'h3c00;
    invalid_exception = 1'b1;
    #1;
    if ((rounded_ieee !== 16'h7e00) || (rounding_flags !== 5'b10000))
      $fatal(1, "invalid exception override failed: output=%h flags=%h", rounded_ieee, rounding_flags);
    invalid_exception = 1'b0;
    infinite_exception = 1'b1;
    a = 16'hbc00;
    #1;
    if ((rounded_ieee !== 16'hfc00) || (rounding_flags !== 5'b01000))
      $fatal(1, "infinite exception override failed: output=%h flags=%h", rounded_ieee, rounding_flags);
    infinite_exception = 1'b0;

    check_round(16'h3c00, 2'b10, 3'b000, 16'h3c00, 5'b00001);
    check_round(16'h3c00, 2'b10, 3'b100, 16'h3c01, 5'b00001);
    check_round(16'h3c00, 2'b10, 3'b011, 16'h3c01, 5'b00001);
    check_round(16'h3c00, 2'b10, 3'b010, 16'h3c00, 5'b00001);
    check_round(16'h3c00, 2'b10, 3'b110, 16'h3c01, 5'b00001);
    check_round(16'h3c01, 2'b10, 3'b000, 16'h3c02, 5'b00001);
    check_round(16'hbc00, 2'b10, 3'b010, 16'hbc01, 5'b00001);
    check_round(16'hbc00, 2'b10, 3'b011, 16'hbc00, 5'b00001);
    check_round(16'h7bff, 2'b10, 3'b000, 16'h7c00, 5'b00101);
    check_round(16'h7bff, 2'b10, 3'b001, 16'h7bff, 5'b00001);
    rounding_extra = 2'b00;

    check_integer_to_float(16'h0000, 1'b0, 3'b000, 16'h0000, 5'b00000);
    check_integer_to_float(16'h0001, 1'b0, 3'b000, 16'h3c00, 5'b00000);
    check_integer_to_float(16'hffff, 1'b1, 3'b000, 16'hbc00, 5'b00000);
    check_integer_to_float(16'h0800, 1'b0, 3'b000, 16'h6800, 5'b00000);
    check_integer_to_float(16'h0801, 1'b0, 3'b000, 16'h6800, 5'b00001);
    check_integer_to_float(16'h0801, 1'b0, 3'b100, 16'h6801, 5'b00001);
    check_integer_to_float(16'hffff, 1'b0, 3'b000, 16'h7c00, 5'b00101);
    check_integer_to_float(16'hffff, 1'b0, 3'b001, 16'h7bff, 5'b00001);

    check_float_to_integer(16'h3c00, 1'b1, 3'b000, 16'h0001, 3'b000);
    check_float_to_integer(16'hbc00, 1'b1, 3'b000, 16'hffff, 3'b000);
    check_float_to_integer(16'h3e00, 1'b1, 3'b000, 16'h0002, 3'b001);
    check_float_to_integer(16'h4100, 1'b1, 3'b000, 16'h0002, 3'b001);
    check_float_to_integer(16'h4100, 1'b1, 3'b100, 16'h0003, 3'b001);
    check_float_to_integer(16'hbe00, 1'b1, 3'b010, 16'hfffe, 3'b001);
    check_float_to_integer(16'h7bff, 1'b0, 3'b000, 16'hffe0, 3'b000);
    check_float_to_integer(16'h7bff, 1'b1, 3'b000, 16'h7fff, 3'b010);
    check_float_to_integer(16'h7c00, 1'b1, 3'b000, 16'h7fff, 3'b100);
    check_float_to_integer(16'h7e00, 1'b1, 3'b000, 16'h7fff, 3'b100);
    check_float_to_integer(16'hbc00, 1'b0, 3'b000, 16'h0000, 3'b010);

    a = 16'h3c00;
    rounding_mode = 3'b000;
    #1;
    if ((widened_ieee !== 32'h3f800000) || (widened_flags !== 5'b00000))
      $fatal(1, "RecFN widening failed: output=%h flags=%h", widened_ieee, widened_flags);
    a = 16'h0001;
    #1;
    if ((widened_ieee !== 32'h33800000) || (widened_flags !== 5'b00000))
      $fatal(1, "subnormal RecFN widening failed: output=%h flags=%h", widened_ieee, widened_flags);
    a = 16'h7d00;
    #1;
    if ((widened_ieee !== 32'h7fc00000) || (widened_flags !== 5'b10000))
      $fatal(1, "signaling NaN RecFN widening failed: output=%h flags=%h", widened_ieee, widened_flags);

    check_narrow(32'h3f800000, 3'b000, 16'h3c00, 5'b00000);
    check_narrow(32'h3f801000, 3'b000, 16'h3c00, 5'b00001);
    check_narrow(32'h3f801000, 3'b100, 16'h3c01, 5'b00001);
    check_narrow(32'h7f800000, 3'b000, 16'h7c00, 5'b00000);
    check_narrow(32'h7fc00000, 3'b000, 16'h7e00, 5'b00000);
    check_narrow(32'h7fa00000, 3'b000, 16'h7e00, 5'b10000);
    check_narrow(32'h7f7fffff, 3'b000, 16'h7c00, 5'b00101);

    check_multiply(16'h3c00, 16'h4000, 3'b000, 16'h4000, 5'b00000);
    check_multiply(16'hbc00, 16'h4000, 3'b000, 16'hc000, 5'b00000);
    check_multiply(16'h3e00, 16'h4000, 3'b000, 16'h4200, 5'b00000);
    check_multiply(16'h8000, 16'h4000, 3'b000, 16'h8000, 5'b00000);
    check_multiply(16'h0000, 16'h7c00, 3'b000, 16'h7e00, 5'b10000);
    check_multiply(16'h7d00, 16'h3c00, 3'b000, 16'h7e00, 5'b10000);
    check_multiply(16'h7e00, 16'h3c00, 3'b000, 16'h7e00, 5'b00000);
    check_multiply(16'h7c00, 16'h4000, 3'b000, 16'h7c00, 5'b00000);
    check_multiply(16'h7bff, 16'h4000, 3'b000, 16'h7c00, 5'b00101);
    check_multiply(16'h7bff, 16'h4000, 3'b001, 16'h7bff, 5'b00101);
    check_multiply(16'h0400, 16'h3800, 3'b000, 16'h0200, 5'b00000);
    check_multiply(16'h0001, 16'h3800, 3'b000, 16'h0000, 5'b00011);
    check_multiply(16'h0001, 16'h3800, 3'b100, 16'h0001, 5'b00011);
    check_multiply(16'h3c01, 16'h3c01, 3'b000, 16'h3c02, 5'b00001);

    check_add(16'h3c00, 16'h4000, 1'b0, 3'b000, 16'h4200, 5'b00000);
    check_add(16'h3c00, 16'h4000, 1'b1, 3'b000, 16'hbc00, 5'b00000);
    check_add(16'hbc00, 16'h4000, 1'b0, 3'b000, 16'h3c00, 5'b00000);
    check_add(16'h3c01, 16'h3c00, 1'b1, 3'b000, 16'h1400, 5'b00000);
    check_add(16'h3c00, 16'h3c00, 1'b1, 3'b000, 16'h0000, 5'b00000);
    check_add(16'h3c00, 16'h3c00, 1'b1, 3'b010, 16'h8000, 5'b00000);
    check_add(16'h0000, 16'h8000, 1'b0, 3'b000, 16'h0000, 5'b00000);
    check_add(16'h0000, 16'h8000, 1'b0, 3'b010, 16'h8000, 5'b00000);
    check_add(16'h8000, 16'h8000, 1'b0, 3'b000, 16'h8000, 5'b00000);
    check_add(16'h0001, 16'h0001, 1'b0, 3'b000, 16'h0002, 5'b00000);
    check_add(16'h3c00, 16'h0001, 1'b0, 3'b000, 16'h3c00, 5'b00001);
    check_add(16'h3c00, 16'h0001, 1'b0, 3'b011, 16'h3c01, 5'b00001);
    check_add(16'h3c00, 16'h0001, 1'b1, 3'b000, 16'h3c00, 5'b00001);
    check_add(16'h3c00, 16'h0001, 1'b1, 3'b010, 16'h3bff, 5'b00001);
    check_add(16'h3c00, 16'h1000, 1'b0, 3'b000, 16'h3c00, 5'b00001);
    check_add(16'h3c00, 16'h1000, 1'b0, 3'b100, 16'h3c01, 5'b00001);
    check_add(16'h3c00, 16'h1000, 1'b0, 3'b110, 16'h3c01, 5'b00001);
    check_add(16'h3c01, 16'h1000, 1'b0, 3'b000, 16'h3c02, 5'b00001);
    check_add(16'hbc00, 16'h9000, 1'b0, 3'b010, 16'hbc01, 5'b00001);
    check_add(16'hbc00, 16'h9000, 1'b0, 3'b011, 16'hbc00, 5'b00001);
    check_add(16'h7bff, 16'h7bff, 1'b0, 3'b000, 16'h7c00, 5'b00101);
    check_add(16'h7bff, 16'h7bff, 1'b0, 3'b001, 16'h7bff, 5'b00101);
    check_add(16'h7c00, 16'hfc00, 1'b0, 3'b000, 16'h7e00, 5'b10000);
    check_add(16'h7c00, 16'h7c00, 1'b1, 3'b000, 16'h7e00, 5'b10000);
    check_add(16'h7c00, 16'h7c00, 1'b0, 3'b000, 16'h7c00, 5'b00000);
    check_add(16'h7d00, 16'h3c00, 1'b0, 3'b000, 16'h7e00, 5'b10000);
    check_add(16'h7e00, 16'h3c00, 1'b0, 3'b000, 16'h7e00, 5'b00000);

    check_fma(16'h3c00, 16'h4000, 16'h4200, 2'b00, 3'b000, 16'h4500, 5'b00000);
    check_fma(16'h3c00, 16'h4000, 16'h4200, 2'b01, 3'b000, 16'hbc00, 5'b00000);
    check_fma(16'h3c00, 16'h4000, 16'h4200, 2'b10, 3'b000, 16'h3c00, 5'b00000);
    check_fma(16'h3c00, 16'h4000, 16'h4200, 2'b11, 3'b000, 16'hc500, 5'b00000);
    check_fma(16'h3c01, 16'h3bff, 16'hbc00, 2'b00, 3'b000, 16'h0ffe, 5'b00000);
    check_fma(16'h0000, 16'h7c00, 16'h3c00, 2'b00, 3'b000, 16'h7e00, 5'b10000);
    check_fma(16'h7c00, 16'h4000, 16'hfc00, 2'b00, 3'b000, 16'h7e00, 5'b10000);
    check_fma(16'h7d00, 16'h3c00, 16'h3c00, 2'b00, 3'b000, 16'h7e00, 5'b10000);
    check_fma(16'h7e00, 16'h3c00, 16'h3c00, 2'b00, 3'b000, 16'h7e00, 5'b00000);
    check_fma(16'h7bff, 16'h4000, 16'h0000, 2'b00, 3'b000, 16'h7c00, 5'b00101);
    check_fma(16'h0001, 16'h3800, 16'h0000, 2'b00, 3'b100, 16'h0001, 5'b00011);

    a = 16'h00f0;
    b = 16'h0000;
    signaling = 1'b0;
    #1;
    if (leading_zero_count !== 4'd2)
      $fatal(1, "leading zero count failed: got=%0d", leading_zero_count);
    $display("hardfloat representation, conversion, rounding, and arithmetic simulation passed");
    $finish;
  end
endmodule
