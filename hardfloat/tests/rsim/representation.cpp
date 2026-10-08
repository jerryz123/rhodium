// Checks permanent HardFloat representation, conversion, rounding, and
// arithmetic behavior.
// SPDX-License-Identifier: BSD-3-Clause
#include "flags.hpp"
#include "test.hpp"
int ieee_value;
int low_mask_input;

void check_case(std::uint16_t next_a, std::uint16_t next_b, bool next_signaling,
                std::uint16_t expected_class, bool expected_lt,
                bool expected_eq, bool expected_gt, bool expected_invalid) {
  {
    a = next_a;
    b = next_b;
    signaling = next_signaling;
    eval();
    if (restored_a != next_a)
      fail(1, "F16 round trip failed: input=%h output=%h", next_a, restored_a);
    if (classification != expected_class)
      fail(1, "classification failed: input=%h class=%h expected=%h", next_a,
           classification, expected_class);
    if (lt != expected_lt || eq != expected_eq || gt != expected_gt)
      fail(1, "comparison failed: a=%h b=%h got=%b%b%b", next_a, next_b, lt, eq,
           gt);
    if (flag_bits(exception_flags) != (std::uint64_t(expected_invalid) << 4))
      fail(1, "exception flags failed: got=%h", flag_bits(exception_flags));
  }
}

void check_fma(std::uint16_t next_a, std::uint16_t next_b, std::uint16_t next_c,
               std::uint8_t next_operation, std::uint8_t next_mode,
               std::uint16_t expected_ieee, std::uint8_t expected_flags) {
  {
    a = next_a;
    b = next_b;
    fma_c = next_c;
    fma_operation = next_operation;
    rounding_mode = next_mode;
    eval();
    if ((fma_ieee != expected_ieee) || (flag_bits(fma_flags) != expected_flags))
      fail(1,
           "fused multiply-add failed: a=%h b=%h c=%h operation=%h mode=%h "
           "output=%h flags=%h expected=%h/%h",
           next_a, next_b, next_c, next_operation, next_mode, fma_ieee,
           flag_bits(fma_flags), expected_ieee, expected_flags);
  }
}

void check_multiply(std::uint16_t next_a, std::uint16_t next_b,
                    std::uint8_t next_mode, std::uint16_t expected_ieee,
                    std::uint8_t expected_flags) {
  {
    a = next_a;
    b = next_b;
    rounding_mode = next_mode;
    eval();
    if ((product_ieee != expected_ieee) ||
        (flag_bits(product_flags) != expected_flags))
      fail(1,
           "multiply failed: a=%h b=%h mode=%h output=%h flags=%h "
           "expected=%h/%h",
           next_a, next_b, next_mode, product_ieee, flag_bits(product_flags),
           expected_ieee, expected_flags);
  }
}

void check_add(std::uint16_t next_a, std::uint16_t next_b, bool next_subtract,
               std::uint8_t next_mode, std::uint16_t expected_ieee,
               std::uint8_t expected_flags) {
  {
    a = next_a;
    b = next_b;
    add_subtract = next_subtract;
    rounding_mode = next_mode;
    eval();
    if ((sum_ieee != expected_ieee) || (flag_bits(sum_flags) != expected_flags))
      fail(1,
           "add failed: a=%h b=%h subtract=%b mode=%h output=%h flags=%h "
           "expected=%h/%h",
           next_a, next_b, next_subtract, next_mode, sum_ieee,
           flag_bits(sum_flags), expected_ieee, expected_flags);
  }
}

void check_integer_to_float(std::uint16_t next_value, bool next_signed,
                            std::uint8_t next_mode, std::uint16_t expected_ieee,
                            std::uint8_t expected_flags) {
  {
    integer_value = next_value;
    integer_signed = next_signed;
    rounding_mode = next_mode;
    eval();
    if ((integer_as_ieee != expected_ieee) ||
        (flag_bits(integer_conversion_flags) != expected_flags))
      fail(1,
           "int-to-float failed: input=%h signed=%b mode=%h output=%h flags=%h "
           "expected=%h/%h",
           next_value, next_signed, next_mode, integer_as_ieee,
           flag_bits(integer_conversion_flags), expected_ieee, expected_flags);
  }
}

void check_float_to_integer(std::uint16_t next_ieee, bool next_signed,
                            std::uint8_t next_mode,
                            std::uint16_t expected_integer,
                            std::uint8_t expected_flags) {
  {
    a = next_ieee;
    integer_output_signed = next_signed;
    rounding_mode = next_mode;
    eval();
    if ((ieee_as_integer != expected_integer) ||
        (flag_bits(integer_result_flags) != expected_flags))
      fail(1,
           "float-to-int failed: input=%h signed=%b mode=%h output=%h flags=%h "
           "expected=%h/%h",
           next_ieee, next_signed, next_mode, ieee_as_integer,
           flag_bits(integer_result_flags), expected_integer, expected_flags);
  }
}

void check_narrow(std::uint32_t next_ieee, std::uint8_t next_mode,
                  std::uint16_t expected_ieee, std::uint8_t expected_flags) {
  {
    wide_ieee = next_ieee;
    rounding_mode = next_mode;
    eval();
    if ((narrowed_ieee != expected_ieee) ||
        (flag_bits(narrowed_flags) != expected_flags))
      fail(1,
           "RecFN narrowing failed: input=%h mode=%h output=%h flags=%h "
           "expected=%h/%h",
           next_ieee, next_mode, narrowed_ieee, flag_bits(narrowed_flags),
           expected_ieee, expected_flags);
  }
}

std::uint16_t low_ones(int count) {
  std::uint16_t return_value{};

  {
    if (count <= 0)
      return_value = UINT64_C(0);
    else if (count >= 12)
      return_value = UINT64_C(4095);
    else
      return_value = (UINT64_C(1) << count) - UINT64_C(1);
  }

  return return_value;
}

void check_round(std::uint16_t next_a, std::uint8_t next_extra,
                 std::uint8_t next_mode, std::uint16_t expected_ieee,
                 std::uint8_t expected_flags) {
  {
    a = next_a;
    rounding_extra = next_extra;
    rounding_mode = next_mode;
    eval();
    if ((rounded_ieee != expected_ieee) ||
        (flag_bits(rounding_flags) != expected_flags))
      fail(1,
           "rounding failed: input=%h extra=%h mode=%h output=%h flags=%h "
           "expected=%h/%h",
           next_a, next_extra, next_mode, rounded_ieee,
           flag_bits(rounding_flags), expected_ieee, expected_flags);
  }
}

int main() {
  return run_test([] {
    invalid_exception = UINT64_C(0);
    infinite_exception = UINT64_C(0);
    rounding_mode = UINT64_C(0);
    tininess_mode = UINT64_C(1);
    rounding_extra = UINT64_C(0);
    integer_value = UINT64_C(0);
    integer_signed = UINT64_C(0);
    integer_output_signed = UINT64_C(0);
    wide_ieee = UINT64_C(0);
    add_subtract = UINT64_C(0);
    fma_c = UINT64_C(0);
    fma_operation = UINT64_C(0);
    check_case(UINT64_C(0), UINT64_C(32768), UINT64_C(0), UINT64_C(16),
               UINT64_C(0), UINT64_C(1), UINT64_C(0), UINT64_C(0));
    check_case(UINT64_C(32768), UINT64_C(15360), UINT64_C(0), UINT64_C(8),
               UINT64_C(1), UINT64_C(0), UINT64_C(0), UINT64_C(0));
    check_case(UINT64_C(15360), UINT64_C(16384), UINT64_C(0), UINT64_C(64),
               UINT64_C(1), UINT64_C(0), UINT64_C(0), UINT64_C(0));
    check_case(UINT64_C(31744), UINT64_C(64512), UINT64_C(0), UINT64_C(128),
               UINT64_C(0), UINT64_C(0), UINT64_C(1), UINT64_C(0));
    check_case(UINT64_C(32256), UINT64_C(15360), UINT64_C(0), UINT64_C(512),
               UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0));
    check_case(UINT64_C(32000), UINT64_C(15360), UINT64_C(0), UINT64_C(256),
               UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(1));
    check_case(UINT64_C(32256), UINT64_C(15360), UINT64_C(1), UINT64_C(512),
               UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(1));

    b = UINT64_C(0);
    add_subtract = UINT64_C(0);
    for (ieee_value = 0; ieee_value < 65536; ieee_value = ieee_value + 1) {
      a = slice(ieee_value, 15, 0);
      low_mask_input = (slice(a, 5, 0));
      eval();
      if (restored_a != a)
        fail(1, "exhaustive F16 round trip failed: input=%h output=%h", a,
             restored_a);
      if ((slice(a, 14, 10) == UINT64_C(31)) &&
          (slice(a, 9, 0) != UINT64_C(0))) {
        if (rounded_ieee != UINT64_C(32256))
          fail(1, "NaN canonicalization failed: input=%h output=%h", a,
               rounded_ieee);
      } else if (rounded_ieee != a) {
        fail(1, "resize and rounding identity failed: input=%h output=%h", a,
             rounded_ieee);
      }
      if (flag_bits(rounding_flags) != UINT64_C(0))
        fail(1, "unexpected identity rounding flags: input=%h flags=%h", a,
             flag_bits(rounding_flags));
      if (descending_low_mask != low_ones(18 - low_mask_input))
        fail(1, "descending low mask failed: input=%h mask=%h", slice(a, 5, 0),
             descending_low_mask);
      if (ascending_low_mask != low_ones(low_mask_input - 6))
        fail(1, "ascending low mask failed: input=%h mask=%h", slice(a, 5, 0),
             ascending_low_mask);
      if ((slice(a, 14, 10) == UINT64_C(31)) &&
          (slice(a, 9, 0) != UINT64_C(0))) {
        if ((sum_ieee != UINT64_C(32256)) ||
            (flag_bits(sum_flags) !=
             (std::uint64_t(((a >> (9)) & 1) == UINT64_C(0)) << 4)))
          fail(1, "exhaustive add-zero NaN failed: input=%h output=%h flags=%h",
               a, sum_ieee, flag_bits(sum_flags));
      } else if (a == UINT64_C(32768)) {
        if ((sum_ieee != UINT64_C(0)) || (flag_bits(sum_flags) != UINT64_C(0)))
          fail(1, "negative-zero plus zero failed: output=%h flags=%h",
               sum_ieee, flag_bits(sum_flags));
      } else if ((sum_ieee != a) || (flag_bits(sum_flags) != UINT64_C(0))) {
        fail(1,
             "exhaustive add-zero identity failed: input=%h output=%h flags=%h",
             a, sum_ieee, flag_bits(sum_flags));
      }
    }

    add_subtract = UINT64_C(1);
    for (ieee_value = 0; ieee_value < 65536; ieee_value = ieee_value + 1) {
      a = slice(ieee_value, 15, 0);
      b = slice(ieee_value, 15, 0);
      eval();
      if (slice(a, 14, 10) == UINT64_C(31)) {
        if ((sum_ieee != UINT64_C(32256)) ||
            (flag_bits(sum_flags) !=
             (std::uint64_t((slice(a, 9, 0) == UINT64_C(0)) ||
                            (((a >> (9)) & 1) == UINT64_C(0)))
              << 4)))
          fail(1,
               "exhaustive subtract-self special case failed: input=%h "
               "output=%h flags=%h",
               a, sum_ieee, flag_bits(sum_flags));
      } else if ((sum_ieee != UINT64_C(0)) ||
                 (flag_bits(sum_flags) != UINT64_C(0))) {
        fail(1,
             "exhaustive subtract-self cancellation failed: input=%h output=%h "
             "flags=%h",
             a, sum_ieee, flag_bits(sum_flags));
      }
    }
    add_subtract = UINT64_C(0);

    a = UINT64_C(15360);
    invalid_exception = UINT64_C(1);
    eval();
    if ((rounded_ieee != UINT64_C(32256)) ||
        (flag_bits(rounding_flags) != UINT64_C(16)))
      fail(1, "invalid exception override failed: output=%h flags=%h",
           rounded_ieee, flag_bits(rounding_flags));
    invalid_exception = UINT64_C(0);
    infinite_exception = UINT64_C(1);
    a = UINT64_C(48128);
    eval();
    if ((rounded_ieee != UINT64_C(64512)) ||
        (flag_bits(rounding_flags) != UINT64_C(8)))
      fail(1, "infinite exception override failed: output=%h flags=%h",
           rounded_ieee, flag_bits(rounding_flags));
    infinite_exception = UINT64_C(0);

    check_round(UINT64_C(15360), UINT64_C(2), UINT64_C(0), UINT64_C(15360),
                UINT64_C(1));
    check_round(UINT64_C(15360), UINT64_C(2), UINT64_C(4), UINT64_C(15361),
                UINT64_C(1));
    check_round(UINT64_C(15360), UINT64_C(2), UINT64_C(3), UINT64_C(15361),
                UINT64_C(1));
    check_round(UINT64_C(15360), UINT64_C(2), UINT64_C(2), UINT64_C(15360),
                UINT64_C(1));
    check_round(UINT64_C(15360), UINT64_C(2), UINT64_C(6), UINT64_C(15361),
                UINT64_C(1));
    check_round(UINT64_C(15361), UINT64_C(2), UINT64_C(0), UINT64_C(15362),
                UINT64_C(1));
    check_round(UINT64_C(48128), UINT64_C(2), UINT64_C(2), UINT64_C(48129),
                UINT64_C(1));
    check_round(UINT64_C(48128), UINT64_C(2), UINT64_C(3), UINT64_C(48128),
                UINT64_C(1));
    check_round(UINT64_C(31743), UINT64_C(2), UINT64_C(0), UINT64_C(31744),
                UINT64_C(5));
    check_round(UINT64_C(31743), UINT64_C(2), UINT64_C(1), UINT64_C(31743),
                UINT64_C(1));
    rounding_extra = UINT64_C(0);

    check_integer_to_float(UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
                           UINT64_C(0));
    check_integer_to_float(UINT64_C(1), UINT64_C(0), UINT64_C(0),
                           UINT64_C(15360), UINT64_C(0));
    check_integer_to_float(UINT64_C(65535), UINT64_C(1), UINT64_C(0),
                           UINT64_C(48128), UINT64_C(0));
    check_integer_to_float(UINT64_C(2048), UINT64_C(0), UINT64_C(0),
                           UINT64_C(26624), UINT64_C(0));
    check_integer_to_float(UINT64_C(2049), UINT64_C(0), UINT64_C(0),
                           UINT64_C(26624), UINT64_C(1));
    check_integer_to_float(UINT64_C(2049), UINT64_C(0), UINT64_C(4),
                           UINT64_C(26625), UINT64_C(1));
    check_integer_to_float(UINT64_C(65535), UINT64_C(0), UINT64_C(0),
                           UINT64_C(31744), UINT64_C(5));
    check_integer_to_float(UINT64_C(65535), UINT64_C(0), UINT64_C(1),
                           UINT64_C(31743), UINT64_C(1));

    check_float_to_integer(UINT64_C(15360), UINT64_C(1), UINT64_C(0),
                           UINT64_C(1), UINT64_C(0));
    check_float_to_integer(UINT64_C(48128), UINT64_C(1), UINT64_C(0),
                           UINT64_C(65535), UINT64_C(0));
    check_float_to_integer(UINT64_C(15872), UINT64_C(1), UINT64_C(0),
                           UINT64_C(2), UINT64_C(1));
    check_float_to_integer(UINT64_C(16640), UINT64_C(1), UINT64_C(0),
                           UINT64_C(2), UINT64_C(1));
    check_float_to_integer(UINT64_C(16640), UINT64_C(1), UINT64_C(4),
                           UINT64_C(3), UINT64_C(1));
    check_float_to_integer(UINT64_C(48640), UINT64_C(1), UINT64_C(2),
                           UINT64_C(65534), UINT64_C(1));
    check_float_to_integer(UINT64_C(31743), UINT64_C(0), UINT64_C(0),
                           UINT64_C(65504), UINT64_C(0));
    check_float_to_integer(UINT64_C(31743), UINT64_C(1), UINT64_C(0),
                           UINT64_C(32767), UINT64_C(2));
    check_float_to_integer(UINT64_C(31744), UINT64_C(1), UINT64_C(0),
                           UINT64_C(32767), UINT64_C(4));
    check_float_to_integer(UINT64_C(32256), UINT64_C(1), UINT64_C(0),
                           UINT64_C(32767), UINT64_C(4));
    check_float_to_integer(UINT64_C(48128), UINT64_C(0), UINT64_C(0),
                           UINT64_C(0), UINT64_C(2));

    a = UINT64_C(15360);
    rounding_mode = UINT64_C(0);
    eval();
    if ((widened_ieee != UINT64_C(1065353216)) ||
        (flag_bits(widened_flags) != UINT64_C(0)))
      fail(1, "RecFN widening failed: output=%h flags=%h", widened_ieee,
           flag_bits(widened_flags));
    a = UINT64_C(1);
    eval();
    if ((widened_ieee != UINT64_C(864026624)) ||
        (flag_bits(widened_flags) != UINT64_C(0)))
      fail(1, "subnormal RecFN widening failed: output=%h flags=%h",
           widened_ieee, flag_bits(widened_flags));
    a = UINT64_C(32000);
    eval();
    if ((widened_ieee != UINT64_C(2143289344)) ||
        (flag_bits(widened_flags) != UINT64_C(16)))
      fail(1, "signaling NaN RecFN widening failed: output=%h flags=%h",
           widened_ieee, flag_bits(widened_flags));

    check_narrow(UINT64_C(1065353216), UINT64_C(0), UINT64_C(15360),
                 UINT64_C(0));
    check_narrow(UINT64_C(1065357312), UINT64_C(0), UINT64_C(15360),
                 UINT64_C(1));
    check_narrow(UINT64_C(1065357312), UINT64_C(4), UINT64_C(15361),
                 UINT64_C(1));
    check_narrow(UINT64_C(2139095040), UINT64_C(0), UINT64_C(31744),
                 UINT64_C(0));
    check_narrow(UINT64_C(2143289344), UINT64_C(0), UINT64_C(32256),
                 UINT64_C(0));
    check_narrow(UINT64_C(2141192192), UINT64_C(0), UINT64_C(32256),
                 UINT64_C(16));
    check_narrow(UINT64_C(2139095039), UINT64_C(0), UINT64_C(31744),
                 UINT64_C(5));

    check_multiply(UINT64_C(15360), UINT64_C(16384), UINT64_C(0),
                   UINT64_C(16384), UINT64_C(0));
    check_multiply(UINT64_C(48128), UINT64_C(16384), UINT64_C(0),
                   UINT64_C(49152), UINT64_C(0));
    check_multiply(UINT64_C(15872), UINT64_C(16384), UINT64_C(0),
                   UINT64_C(16896), UINT64_C(0));
    check_multiply(UINT64_C(32768), UINT64_C(16384), UINT64_C(0),
                   UINT64_C(32768), UINT64_C(0));
    check_multiply(UINT64_C(0), UINT64_C(31744), UINT64_C(0), UINT64_C(32256),
                   UINT64_C(16));
    check_multiply(UINT64_C(32000), UINT64_C(15360), UINT64_C(0),
                   UINT64_C(32256), UINT64_C(16));
    check_multiply(UINT64_C(32256), UINT64_C(15360), UINT64_C(0),
                   UINT64_C(32256), UINT64_C(0));
    check_multiply(UINT64_C(31744), UINT64_C(16384), UINT64_C(0),
                   UINT64_C(31744), UINT64_C(0));
    check_multiply(UINT64_C(31743), UINT64_C(16384), UINT64_C(0),
                   UINT64_C(31744), UINT64_C(5));
    check_multiply(UINT64_C(31743), UINT64_C(16384), UINT64_C(1),
                   UINT64_C(31743), UINT64_C(5));
    check_multiply(UINT64_C(1024), UINT64_C(14336), UINT64_C(0), UINT64_C(512),
                   UINT64_C(0));
    check_multiply(UINT64_C(1), UINT64_C(14336), UINT64_C(0), UINT64_C(0),
                   UINT64_C(3));
    check_multiply(UINT64_C(1), UINT64_C(14336), UINT64_C(4), UINT64_C(1),
                   UINT64_C(3));
    check_multiply(UINT64_C(15361), UINT64_C(15361), UINT64_C(0),
                   UINT64_C(15362), UINT64_C(1));

    check_add(UINT64_C(15360), UINT64_C(16384), UINT64_C(0), UINT64_C(0),
              UINT64_C(16896), UINT64_C(0));
    check_add(UINT64_C(15360), UINT64_C(16384), UINT64_C(1), UINT64_C(0),
              UINT64_C(48128), UINT64_C(0));
    check_add(UINT64_C(48128), UINT64_C(16384), UINT64_C(0), UINT64_C(0),
              UINT64_C(15360), UINT64_C(0));
    check_add(UINT64_C(15361), UINT64_C(15360), UINT64_C(1), UINT64_C(0),
              UINT64_C(5120), UINT64_C(0));
    check_add(UINT64_C(15360), UINT64_C(15360), UINT64_C(1), UINT64_C(0),
              UINT64_C(0), UINT64_C(0));
    check_add(UINT64_C(15360), UINT64_C(15360), UINT64_C(1), UINT64_C(2),
              UINT64_C(32768), UINT64_C(0));
    check_add(UINT64_C(0), UINT64_C(32768), UINT64_C(0), UINT64_C(0),
              UINT64_C(0), UINT64_C(0));
    check_add(UINT64_C(0), UINT64_C(32768), UINT64_C(0), UINT64_C(2),
              UINT64_C(32768), UINT64_C(0));
    check_add(UINT64_C(32768), UINT64_C(32768), UINT64_C(0), UINT64_C(0),
              UINT64_C(32768), UINT64_C(0));
    check_add(UINT64_C(1), UINT64_C(1), UINT64_C(0), UINT64_C(0), UINT64_C(2),
              UINT64_C(0));
    check_add(UINT64_C(15360), UINT64_C(1), UINT64_C(0), UINT64_C(0),
              UINT64_C(15360), UINT64_C(1));
    check_add(UINT64_C(15360), UINT64_C(1), UINT64_C(0), UINT64_C(3),
              UINT64_C(15361), UINT64_C(1));
    check_add(UINT64_C(15360), UINT64_C(1), UINT64_C(1), UINT64_C(0),
              UINT64_C(15360), UINT64_C(1));
    check_add(UINT64_C(15360), UINT64_C(1), UINT64_C(1), UINT64_C(2),
              UINT64_C(15359), UINT64_C(1));
    check_add(UINT64_C(15360), UINT64_C(4096), UINT64_C(0), UINT64_C(0),
              UINT64_C(15360), UINT64_C(1));
    check_add(UINT64_C(15360), UINT64_C(4096), UINT64_C(0), UINT64_C(4),
              UINT64_C(15361), UINT64_C(1));
    check_add(UINT64_C(15360), UINT64_C(4096), UINT64_C(0), UINT64_C(6),
              UINT64_C(15361), UINT64_C(1));
    check_add(UINT64_C(15361), UINT64_C(4096), UINT64_C(0), UINT64_C(0),
              UINT64_C(15362), UINT64_C(1));
    check_add(UINT64_C(48128), UINT64_C(36864), UINT64_C(0), UINT64_C(2),
              UINT64_C(48129), UINT64_C(1));
    check_add(UINT64_C(48128), UINT64_C(36864), UINT64_C(0), UINT64_C(3),
              UINT64_C(48128), UINT64_C(1));
    check_add(UINT64_C(31743), UINT64_C(31743), UINT64_C(0), UINT64_C(0),
              UINT64_C(31744), UINT64_C(5));
    check_add(UINT64_C(31743), UINT64_C(31743), UINT64_C(0), UINT64_C(1),
              UINT64_C(31743), UINT64_C(5));
    check_add(UINT64_C(31744), UINT64_C(64512), UINT64_C(0), UINT64_C(0),
              UINT64_C(32256), UINT64_C(16));
    check_add(UINT64_C(31744), UINT64_C(31744), UINT64_C(1), UINT64_C(0),
              UINT64_C(32256), UINT64_C(16));
    check_add(UINT64_C(31744), UINT64_C(31744), UINT64_C(0), UINT64_C(0),
              UINT64_C(31744), UINT64_C(0));
    check_add(UINT64_C(32000), UINT64_C(15360), UINT64_C(0), UINT64_C(0),
              UINT64_C(32256), UINT64_C(16));
    check_add(UINT64_C(32256), UINT64_C(15360), UINT64_C(0), UINT64_C(0),
              UINT64_C(32256), UINT64_C(0));

    check_fma(UINT64_C(15360), UINT64_C(16384), UINT64_C(16896), UINT64_C(0),
              UINT64_C(0), UINT64_C(17664), UINT64_C(0));
    check_fma(UINT64_C(15360), UINT64_C(16384), UINT64_C(16896), UINT64_C(1),
              UINT64_C(0), UINT64_C(48128), UINT64_C(0));
    check_fma(UINT64_C(15360), UINT64_C(16384), UINT64_C(16896), UINT64_C(2),
              UINT64_C(0), UINT64_C(15360), UINT64_C(0));
    check_fma(UINT64_C(15360), UINT64_C(16384), UINT64_C(16896), UINT64_C(3),
              UINT64_C(0), UINT64_C(50432), UINT64_C(0));
    check_fma(UINT64_C(15361), UINT64_C(15359), UINT64_C(48128), UINT64_C(0),
              UINT64_C(0), UINT64_C(4094), UINT64_C(0));
    check_fma(UINT64_C(0), UINT64_C(31744), UINT64_C(15360), UINT64_C(0),
              UINT64_C(0), UINT64_C(32256), UINT64_C(16));
    check_fma(UINT64_C(31744), UINT64_C(16384), UINT64_C(64512), UINT64_C(0),
              UINT64_C(0), UINT64_C(32256), UINT64_C(16));
    check_fma(UINT64_C(32000), UINT64_C(15360), UINT64_C(15360), UINT64_C(0),
              UINT64_C(0), UINT64_C(32256), UINT64_C(16));
    check_fma(UINT64_C(32256), UINT64_C(15360), UINT64_C(15360), UINT64_C(0),
              UINT64_C(0), UINT64_C(32256), UINT64_C(0));
    check_fma(UINT64_C(31743), UINT64_C(16384), UINT64_C(0), UINT64_C(0),
              UINT64_C(0), UINT64_C(31744), UINT64_C(5));
    check_fma(UINT64_C(1), UINT64_C(14336), UINT64_C(0), UINT64_C(0),
              UINT64_C(4), UINT64_C(1), UINT64_C(3));

    a = UINT64_C(240);
    b = UINT64_C(0);
    signaling = UINT64_C(0);
    eval();
    if (leading_zero_count != UINT64_C(2))
      fail(1, "leading zero count failed: got=%0d", leading_zero_count);
  });
}
