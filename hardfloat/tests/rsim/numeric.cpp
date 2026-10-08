// Exhaustively checks binary16 minimum/maximum, integral rounding, and modulo
// conversion behavior.
// SPDX-License-Identifier: BSD-3-Clause
#include "flags.hpp"
#include "test.hpp"
int value_index;
int operation_index;
int mode_index;

bool f16_nan(std::uint16_t value) {
  bool return_value{};

  return_value = (slice(value, 14, 10) == UINT64_C(31)) &&
                 (slice(value, 9, 0) != UINT64_C(0));

  return return_value;
}

bool f16_signaling_nan(std::uint16_t value) {
  bool return_value{};

  return_value = f16_nan(value) && !((value >> (9)) & 1);

  return return_value;
}

bool f16_less(std::uint16_t left, std::uint16_t right) {
  bool return_value{};

  std::uint16_t left_magnitude;
  std::uint16_t right_magnitude;
  {
    left_magnitude = slice(left, 14, 0);
    right_magnitude = slice(right, 14, 0);
    if ((left_magnitude == 0) && (right_magnitude == 0))
      return_value = UINT64_C(0);
    else if (((left >> (15)) & 1) != ((right >> (15)) & 1))
      return_value = ((left >> (15)) & 1);
    else if (((left >> (15)) & 1))
      return_value = left_magnitude > right_magnitude;
    else
      return_value = left_magnitude < right_magnitude;
  }

  return return_value;
}

std::uint16_t expected_min_max(std::uint16_t left, std::uint16_t right,
                               std::uint8_t operation) {
  std::uint16_t return_value{};

  bool left_nan;
  bool right_nan;
  bool maximum;
  bool prefer_number;
  bool choose_left;
  {
    left_nan = f16_nan(left);
    right_nan = f16_nan(right);
    maximum = ((operation >> (0)) & 1);
    prefer_number = ((operation >> (1)) & 1);
    if ((left_nan && right_nan) || ((left_nan || right_nan) && !prefer_number))
      return_value = UINT64_C(32256);
    else if (left_nan)
      return_value = right;
    else if (right_nan)
      return_value = left;
    else if ((slice(left, 14, 0) == 0) && (slice(right, 14, 0) == 0))
      return_value = (std::uint16_t(maximum ? ((left & right) >> 15)
                                            : ((left | right) >> 15))
                      << 15);
    else {
      choose_left = maximum ? f16_less(right, left) : f16_less(left, right);
      return_value = choose_left ? left : right;
    }
  }

  return return_value;
}

std::uint8_t selected_mode(int index) {
  std::uint8_t return_value{};

  {
    switch (index) {
    case 0:
      return_value = UINT64_C(0);

      break;
    case 1:
      return_value = UINT64_C(1);

      break;
    case 2:
      return_value = UINT64_C(2);

      break;
    case 3:
      return_value = UINT64_C(3);

      break;
    case 4:
      return_value = UINT64_C(4);

      break;
    default:
      return_value = UINT64_C(6);

      break;
    }
  }

  return return_value;
}

std::uint16_t expected_integral(std::uint16_t value, std::uint8_t mode) {
  std::uint16_t return_value{};

  std::uint8_t exponent;
  int fractional_bits;
  std::uint16_t mask;
  std::uint16_t magnitude;
  std::uint16_t truncated;
  bool guard;
  bool sticky;
  bool retained_lsb;
  bool inexact;
  bool increment;
  {
    magnitude = (slice(value, 14, 0));
    exponent = slice(value, 14, 10);
    if (f16_nan(value))
      return_value = UINT64_C(32256);
    else if ((exponent == 31) || (magnitude == 0) || (exponent >= 25))
      return_value = value;
    else if (exponent < 15) {
      inexact = magnitude != 0;
      switch (mode) {
      case UINT64_C(0):
        increment = magnitude > UINT64_C(14336);

        break;
      case UINT64_C(1):
        increment = UINT64_C(0);

        break;
      case UINT64_C(2):
        increment = ((value >> (15)) & 1) && inexact;

        break;
      case UINT64_C(3):
        increment = !((value >> (15)) & 1) && inexact;

        break;
      case UINT64_C(4):
        increment = magnitude >= UINT64_C(14336);

        break;
      case UINT64_C(6):
        increment = inexact;

        break;
      default:
        increment = UINT64_C(0);

        break;
      }
      return_value = ((value & 0x8000u) | (increment ? 0x3c00u : 0u));
    } else {
      fractional_bits = 25 - (exponent);
      mask = UINT64_C(65535) >> (16 - fractional_bits);
      truncated = magnitude & ~mask;
      guard = ((magnitude >> (fractional_bits - 1)) & 1);
      sticky = (magnitude & (UINT64_C(65535) >> (17 - fractional_bits))) != 0;
      retained_lsb = ((magnitude >> (fractional_bits)) & 1);
      inexact = guard || sticky;
      increment =
          ((mode == UINT64_C(0)) && guard && (sticky || retained_lsb)) ||
          ((mode == UINT64_C(4)) && guard) ||
          ((mode == UINT64_C(2)) && ((value >> (15)) & 1) && inexact) ||
          ((mode == UINT64_C(3)) && !((value >> (15)) & 1) && inexact);
      if ((mode == UINT64_C(6)) && inexact)
        truncated = truncated | (UINT64_C(1) << fractional_bits);
      else if (increment)
        truncated = truncated + (UINT64_C(1) << fractional_bits);
      return_value = ((value & 0x8000u) | slice(truncated, 14, 0));
    }
  }

  return return_value;
}

bool expected_integral_inexact(std::uint16_t value) {
  bool return_value{};

  std::uint8_t exponent;
  int fractional_bits;
  std::uint16_t mask;
  {
    exponent = slice(value, 14, 10);
    if ((exponent == 31) || (slice(value, 14, 0) == 0) || (exponent >= 25))
      return_value = UINT64_C(0);
    else if (exponent < 15)
      return_value = UINT64_C(1);
    else {
      fractional_bits = 25 - (exponent);
      mask = UINT64_C(32767) >> (15 - fractional_bits);
      return_value = (slice(value, 14, 0) & mask) != 0;
    }
  }

  return return_value;
}

std::uint8_t expected_modulo(std::uint16_t value, std::uint8_t mode) {
  std::uint8_t return_value{};

  int exponent;
  int significand;
  int shift;
  int magnitude;
  int signed_result;
  bool guard;
  bool sticky;
  bool inexact;
  bool increment;
  {
    if (slice(value, 14, 10) == 31)
      return_value = UINT64_C(0);
    else {
      if (slice(value, 14, 10) == 0) {
        exponent = -14;
        significand = (slice(value, 9, 0));
      } else {
        exponent = (slice(value, 14, 10)) - 15;
        significand = 1024 + (slice(value, 9, 0));
      }
      shift = 10 - exponent;
      if (shift <= 0) {
        magnitude = significand << -shift;
        guard = UINT64_C(0);
        sticky = UINT64_C(0);
      } else {
        magnitude = significand >> shift;
        guard = ((significand >> (shift - 1)) & 1);
        sticky = (significand & ((1 << (shift - 1)) - 1)) != 0;
      }
      inexact = guard || sticky;
      increment = ((mode == UINT64_C(0)) && guard &&
                   (sticky || ((magnitude >> (0)) & 1))) ||
                  ((mode == UINT64_C(4)) && guard) ||
                  ((mode == UINT64_C(2)) && ((value >> (15)) & 1) && inexact) ||
                  ((mode == UINT64_C(3)) && !((value >> (15)) & 1) && inexact);
      if ((mode == UINT64_C(6)) && inexact)
        magnitude = magnitude | 1;
      else if (increment)
        magnitude = magnitude + 1;
      signed_result = ((value >> (15)) & 1) ? -magnitude : magnitude;
      return_value = slice(signed_result, 7, 0);
    }
  }

  return return_value;
}

void check_min_max_range(std::uint16_t right) {
  {
    b = right;
    for (operation_index = 0; operation_index < 4;
         operation_index = operation_index + 1) {
      min_max_operation = slice(operation_index, 1, 0);
      for (value_index = 0; value_index < 65536;
           value_index = value_index + 1) {
        a = slice(value_index, 15, 0);
        eval();
        if (min_max != expected_min_max(a, b, min_max_operation))
          fail(1,
               "min/max mismatch: a=%h b=%h operation=%h output=%h expected=%h",
               a, b, min_max_operation, min_max,
               expected_min_max(a, b, min_max_operation));
        if (flag_bits(min_max_flags) !=
            (std::uint64_t(f16_signaling_nan(a) || f16_signaling_nan(b)) << 4))
          fail(1, "min/max flags mismatch: a=%h b=%h operation=%h flags=%h", a,
               b, min_max_operation, flag_bits(min_max_flags));
      }
    }
  }
}

void check_wide_integral(std::uint64_t value, std::uint8_t mode,
                         bool report_inexact, std::uint64_t expected_value,
                         std::uint8_t expected_flags) {
  {
    wide_value = value;
    rounding_mode = mode;
    raise_inexact = report_inexact;
    eval();
    if ((wide_integral != expected_value) ||
        (flag_bits(wide_integral_flags) != expected_flags))
      fail(1,
           "binary64 integral mismatch: input=%h mode=%h report_inexact=%b "
           "output=%h flags=%h expected=%h/%h",
           value, mode, report_inexact, wide_integral,
           flag_bits(wide_integral_flags), expected_value, expected_flags);
  }
}

void check_wide_modulo(std::uint64_t value, std::uint8_t mode, bool signed_mode,
                       std::uint32_t expected_integer,
                       std::uint8_t expected_flags) {
  {
    wide_value = value;
    rounding_mode = mode;
    signed_output = signed_mode;
    eval();
    if ((wide_modulo_integer != expected_integer) ||
        (flag_bits(wide_modulo_flags) != expected_flags))
      fail(1,
           "binary64 modulo mismatch: input=%h mode=%h signed=%b output=%h "
           "flags=%h expected=%h/%h",
           value, mode, signed_mode, wide_modulo_integer,
           flag_bits(wide_modulo_flags), expected_integer, expected_flags);
  }
}

void check_estimate32(std::uint32_t value, bool operation, std::uint8_t mode,
                      std::uint32_t expected_value,
                      std::uint8_t expected_flags) {
  {
    estimate32_value = value;
    estimate_operation = operation;
    rounding_mode = mode;
    eval();
    if ((estimate32 != expected_value) ||
        (flag_bits(estimate32_flags) != expected_flags))
      fail(1,
           "binary32 estimate mismatch: input=%h operation=%b mode=%h "
           "output=%h flags=%h expected=%h/%h",
           value, operation, mode, estimate32, flag_bits(estimate32_flags),
           expected_value, expected_flags);
  }
}

void check_estimate64(std::uint64_t value, bool operation, std::uint8_t mode,
                      std::uint64_t expected_value,
                      std::uint8_t expected_flags) {
  {
    estimate64_value = value;
    estimate_operation = operation;
    rounding_mode = mode;
    eval();
    if ((estimate64 != expected_value) ||
        (flag_bits(estimate64_flags) != expected_flags))
      fail(1,
           "binary64 estimate mismatch: input=%h operation=%b mode=%h "
           "output=%h flags=%h expected=%h/%h",
           value, operation, mode, estimate64, flag_bits(estimate64_flags),
           expected_value, expected_flags);
  }
}

int main() {
  return run_test([] {
    a = UINT64_C(0);
    b = UINT64_C(0);
    min_max_operation = UINT64_C(0);
    rounding_mode = UINT64_C(0);
    raise_inexact = UINT64_C(1);
    signed_output = UINT64_C(1);
    wide_value = UINT64_C(0);
    estimate32_value = UINT64_C(0);
    estimate64_value = UINT64_C(0);
    estimate_operation = UINT64_C(0);

    check_estimate32(UINT64_C(7441084), UINT64_C(0), UINT64_C(0),
                     UINT64_C(2123366400), UINT64_C(0));
    check_estimate32(UINT64_C(2138461234), UINT64_C(0), UINT64_C(0),
                     UINT64_C(2179072), UINT64_C(0));
    check_estimate32(UINT64_C(7441084), UINT64_C(1), UINT64_C(0),
                     UINT64_C(1594359808), UINT64_C(0));
    check_estimate32(UINT64_C(2138461234), UINT64_C(1), UINT64_C(0),
                     UINT64_C(528613376), UINT64_C(0));
    check_estimate32(UINT64_C(0), UINT64_C(0), UINT64_C(0),
                     UINT64_C(2139095040), UINT64_C(8));
    check_estimate32(UINT64_C(2147483648), UINT64_C(1), UINT64_C(0),
                     UINT64_C(4286578688), UINT64_C(8));
    check_estimate32(UINT64_C(2139095040), UINT64_C(0), UINT64_C(0),
                     UINT64_C(0), UINT64_C(0));
    check_estimate32(UINT64_C(4286578688), UINT64_C(1), UINT64_C(0),
                     UINT64_C(2143289344), UINT64_C(16));
    check_estimate32(UINT64_C(2143289345), UINT64_C(0), UINT64_C(0),
                     UINT64_C(2143289344), UINT64_C(0));
    check_estimate32(UINT64_C(2139095041), UINT64_C(1), UINT64_C(0),
                     UINT64_C(2143289344), UINT64_C(16));
    check_estimate32(UINT64_C(1), UINT64_C(0), UINT64_C(1),
                     UINT64_C(2139095039), UINT64_C(5));
    check_estimate32(UINT64_C(2147483649), UINT64_C(0), UINT64_C(3),
                     UINT64_C(4286578687), UINT64_C(5));
    check_estimate32(UINT64_C(1), UINT64_C(0), UINT64_C(0),
                     UINT64_C(2139095040), UINT64_C(5));

    check_estimate64(UINT64_C(4607182418800017408), UINT64_C(0), UINT64_C(0),
                     UINT64_C(4607147234427928576), UINT64_C(0));
    check_estimate64(UINT64_C(4611686018427387904), UINT64_C(1), UINT64_C(0),
                     UINT64_C(4604508406521266176), UINT64_C(0));
    check_estimate64(UINT64_C(13830554455654793216), UINT64_C(1), UINT64_C(0),
                     UINT64_C(9221120237041090560), UINT64_C(16));

    check_min_max_range(UINT64_C(15360));
    check_min_max_range(UINT64_C(0));
    check_min_max_range(UINT64_C(32768));
    check_min_max_range(UINT64_C(32256));
    check_min_max_range(UINT64_C(32000));

    check_wide_modulo(UINT64_C(4609434218613702656), UINT64_C(1), UINT64_C(1),
                      UINT64_C(1), UINT64_C(1));
    check_wide_modulo(UINT64_C(13832806255468478464), UINT64_C(1), UINT64_C(1),
                      UINT64_C(4294967295), UINT64_C(1));
    check_wide_modulo(UINT64_C(4751297606875873280), UINT64_C(1), UINT64_C(1),
                      UINT64_C(0), UINT64_C(2));
    check_wide_modulo(UINT64_C(4751297606876921856), UINT64_C(1), UINT64_C(1),
                      UINT64_C(1), UINT64_C(2));
    check_wide_modulo(UINT64_C(4980981187871768577), UINT64_C(1), UINT64_C(1),
                      UINT64_C(2147483648), UINT64_C(2));
    check_wide_modulo(UINT64_C(4985484787499139072), UINT64_C(1), UINT64_C(1),
                      UINT64_C(0), UINT64_C(2));
    check_wide_modulo(UINT64_C(9218868437227405311), UINT64_C(1), UINT64_C(1),
                      UINT64_C(0), UINT64_C(2));
    check_wide_modulo(UINT64_C(9218868437227405312), UINT64_C(1), UINT64_C(1),
                      UINT64_C(0), UINT64_C(4));
    check_wide_modulo(UINT64_C(9221120237041090560), UINT64_C(1), UINT64_C(1),
                      UINT64_C(0), UINT64_C(4));
    check_wide_modulo(UINT64_C(4746794007248502784), UINT64_C(1), UINT64_C(0),
                      UINT64_C(2147483648), UINT64_C(0));
    check_wide_modulo(UINT64_C(13830554455654793216), UINT64_C(1), UINT64_C(0),
                      UINT64_C(4294967295), UINT64_C(2));

    check_wide_integral(UINT64_C(4609434218613702656), UINT64_C(0), UINT64_C(1),
                        UINT64_C(4611686018427387904), UINT64_C(1));
    check_wide_integral(UINT64_C(4612811918334230528), UINT64_C(0), UINT64_C(1),
                        UINT64_C(4611686018427387904), UINT64_C(1));
    check_wide_integral(UINT64_C(4612811918334230528), UINT64_C(4), UINT64_C(1),
                        UINT64_C(4613937818241073152), UINT64_C(1));
    check_wide_integral(UINT64_C(13822447976325526323), UINT64_C(2),
                        UINT64_C(1), UINT64_C(13830554455654793216),
                        UINT64_C(1));
    check_wide_integral(UINT64_C(13822447976325526323), UINT64_C(3),
                        UINT64_C(1), UINT64_C(9223372036854775808),
                        UINT64_C(1));
    check_wide_integral(UINT64_C(4877398396442247168), UINT64_C(0), UINT64_C(1),
                        UINT64_C(4877398396442247168), UINT64_C(0));
    check_wide_integral(UINT64_C(9221120237041090561), UINT64_C(0), UINT64_C(1),
                        UINT64_C(9221120237041090560), UINT64_C(0));
    check_wide_integral(UINT64_C(9218868437227405313), UINT64_C(0), UINT64_C(1),
                        UINT64_C(9221120237041090560), UINT64_C(16));
    check_wide_integral(UINT64_C(4609434218613702656), UINT64_C(0), UINT64_C(0),
                        UINT64_C(4611686018427387904), UINT64_C(0));

    raise_inexact = UINT64_C(1);
    signed_output = UINT64_C(1);
    for (mode_index = 0; mode_index < 6; mode_index = mode_index + 1) {
      rounding_mode = selected_mode(mode_index);
      for (value_index = 0; value_index < 65536;
           value_index = value_index + 1) {
        a = slice(value_index, 15, 0);
        eval();
        if (integral != expected_integral(a, rounding_mode))
          fail(1, "integral mismatch: input=%h mode=%h output=%h expected=%h",
               a, rounding_mode, integral, expected_integral(a, rounding_mode));
        if (flag_bits(integral_flags) !=
            ((unsigned(f16_signaling_nan(a)) << 4) |
             unsigned(expected_integral_inexact(a))))
          fail(1, "integral flags mismatch: input=%h mode=%h flags=%h", a,
               rounding_mode, flag_bits(integral_flags));
        if (modulo_integer != expected_modulo(a, rounding_mode))
          fail(1, "modulo mismatch: input=%h mode=%h output=%h expected=%h", a,
               rounding_mode, modulo_integer,
               expected_modulo(a, rounding_mode));
        if (flag_bits(modulo_flags) != flag_bits(standard_flags))
          fail(1,
               "modulo flags differ from standard conversion: input=%h mode=%h "
               "modulo=%h standard=%h",
               a, rounding_mode, flag_bits(modulo_flags),
               flag_bits(standard_flags));
        if ((slice(flag_bits(standard_flags), 2, 1) == UINT64_C(0)) &&
            (modulo_integer != standard_integer))
          fail(1,
               "in-range modulo differs from standard conversion: input=%h "
               "mode=%h modulo=%h standard=%h",
               a, rounding_mode, modulo_integer, standard_integer);
      }
    }

    raise_inexact = UINT64_C(0);
    rounding_mode = UINT64_C(0);
    for (value_index = 0; value_index < 65536; value_index = value_index + 1) {
      a = slice(value_index, 15, 0);
      eval();
      if (flag_bits(integral_flags) !=
          (std::uint64_t(f16_signaling_nan(a)) << 4))
        fail(1, "suppressed integral flags mismatch: input=%h flags=%h", a,
             flag_bits(integral_flags));
    }

    signed_output = UINT64_C(0);
    for (value_index = 0; value_index < 65536; value_index = value_index + 1) {
      a = slice(value_index, 15, 0);
      eval();
      if (flag_bits(modulo_flags) != flag_bits(standard_flags))
        fail(1,
             "unsigned modulo flags differ from standard conversion: input=%h "
             "modulo=%h standard=%h",
             a, flag_bits(modulo_flags), flag_bits(standard_flags));
    }
  });
}
