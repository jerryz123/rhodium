// Checks completed one-bit and two-bit iterative HardFloat division and
// square-root behavior.
// SPDX-License-Identifier: BSD-3-Clause
#include "flags.hpp"
#include "test.hpp"

void check_operation(bool next_square_root, std::uint16_t next_a,
                     std::uint16_t next_b, std::uint8_t next_rounding_mode,
                     std::uint16_t expected_out, std::uint8_t expected_flags,
                     bool expect_two_bit_faster) {
  int elapsed_cycles;
  int one_bit_cycles;
  int two_bit_cycles;
  bool saw_one_bit;
  bool saw_two_bit;
  bool one_bit_event;
  bool two_bit_event;
  {
    while (!input_ready)
      tick_model();
    eval();
    square_root = next_square_root;
    a = next_a;
    b = next_b;
    rounding_mode = next_rounding_mode;
    input_valid = UINT64_C(1);
    tick_model();
    input_valid = UINT64_C(0);
    elapsed_cycles = 0;
    one_bit_cycles = -1;
    two_bit_cycles = -1;
    saw_one_bit = UINT64_C(0);
    saw_two_bit = UINT64_C(0);
    one_bit_event =
        next_square_root ? one_bit_square_root_valid : one_bit_division_valid;
    two_bit_event =
        next_square_root ? two_bit_square_root_valid : two_bit_division_valid;
    if (one_bit_event) {
      saw_one_bit = UINT64_C(1);
      one_bit_cycles = elapsed_cycles;
      if ((one_bit_out != expected_out) ||
          (flag_bits(one_bit_flags) != expected_flags))
        fail(1,
             "one-bool divide/sqrt failed: sqrt=%b a=%h b=%h mode=%h output=%h "
             "flags=%h expected=%h/%h",
             next_square_root, next_a, next_b, next_rounding_mode, one_bit_out,
             flag_bits(one_bit_flags), expected_out, expected_flags);
    }
    if (two_bit_event) {
      saw_two_bit = UINT64_C(1);
      two_bit_cycles = elapsed_cycles;
      if ((two_bit_out != expected_out) ||
          (flag_bits(two_bit_flags) != expected_flags))
        fail(1,
             "two-bool divide/sqrt failed: sqrt=%b a=%h b=%h mode=%h output=%h "
             "flags=%h expected=%h/%h",
             next_square_root, next_a, next_b, next_rounding_mode, two_bit_out,
             flag_bits(two_bit_flags), expected_out, expected_flags);
    }
    while (!(saw_one_bit && saw_two_bit) && (elapsed_cycles < 80)) {
      tick_model();
      elapsed_cycles = elapsed_cycles + 1;
      one_bit_event =
          next_square_root ? one_bit_square_root_valid : one_bit_division_valid;
      two_bit_event =
          next_square_root ? two_bit_square_root_valid : two_bit_division_valid;
      if (one_bit_event && !saw_one_bit) {
        saw_one_bit = UINT64_C(1);
        one_bit_cycles = elapsed_cycles;
        if ((one_bit_out != expected_out) ||
            (flag_bits(one_bit_flags) != expected_flags))
          fail(1,
               "one-bool divide/sqrt failed: sqrt=%b a=%h b=%h mode=%h "
               "output=%h flags=%h expected=%h/%h",
               next_square_root, next_a, next_b, next_rounding_mode,
               one_bit_out, flag_bits(one_bit_flags), expected_out,
               expected_flags);
      }
      if (two_bit_event && !saw_two_bit) {
        saw_two_bit = UINT64_C(1);
        two_bit_cycles = elapsed_cycles;
        if ((two_bit_out != expected_out) ||
            (flag_bits(two_bit_flags) != expected_flags))
          fail(1,
               "two-bool divide/sqrt failed: sqrt=%b a=%h b=%h mode=%h "
               "output=%h flags=%h expected=%h/%h",
               next_square_root, next_a, next_b, next_rounding_mode,
               two_bit_out, flag_bits(two_bit_flags), expected_out,
               expected_flags);
      }
    }
    if (!(saw_one_bit && saw_two_bit))
      fail(1, "divide/sqrt timed out: sqrt=%b a=%h b=%h", next_square_root,
           next_a, next_b);
    if (expect_two_bit_faster && !(two_bit_cycles < one_bit_cycles))
      fail(1, "two-bool configuration did not complete sooner: one=%0d two=%0d",
           one_bit_cycles, two_bit_cycles);
    if (next_square_root && (one_bit_division_valid || two_bit_division_valid))
      fail(1, "square root asserted a division completion");
    if (!next_square_root &&
        (one_bit_square_root_valid || two_bit_square_root_valid))
      fail(1, "division asserted a square-root completion");
  }
}

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    input_valid = UINT64_C(0);
    square_root = UINT64_C(0);
    a = UINT64_C(0);
    b = UINT64_C(0);
    rounding_mode = UINT64_C(0);
    tininess_mode = UINT64_C(1);
    for (unsigned repeat_index = 0; repeat_index < (2); ++repeat_index)
      tick_model();
    eval();
    reset = UINT64_C(0);

    check_operation(UINT64_C(0), UINT64_C(15360), UINT64_C(16384), UINT64_C(0),
                    UINT64_C(14336), UINT64_C(0), UINT64_C(1));
    check_operation(UINT64_C(0), UINT64_C(16896), UINT64_C(16384), UINT64_C(0),
                    UINT64_C(15872), UINT64_C(0), UINT64_C(1));
    check_operation(UINT64_C(0), UINT64_C(48128), UINT64_C(16384), UINT64_C(0),
                    UINT64_C(47104), UINT64_C(0), UINT64_C(1));
    check_operation(UINT64_C(0), UINT64_C(15360), UINT64_C(16896), UINT64_C(0),
                    UINT64_C(13653), UINT64_C(1), UINT64_C(1));
    check_operation(UINT64_C(0), UINT64_C(15360), UINT64_C(16896), UINT64_C(3),
                    UINT64_C(13654), UINT64_C(1), UINT64_C(1));
    check_operation(UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
                    UINT64_C(32256), UINT64_C(16), UINT64_C(0));
    check_operation(UINT64_C(0), UINT64_C(31744), UINT64_C(31744), UINT64_C(0),
                    UINT64_C(32256), UINT64_C(16), UINT64_C(0));
    check_operation(UINT64_C(0), UINT64_C(15360), UINT64_C(0), UINT64_C(0),
                    UINT64_C(31744), UINT64_C(8), UINT64_C(0));
    check_operation(UINT64_C(0), UINT64_C(32768), UINT64_C(16384), UINT64_C(0),
                    UINT64_C(32768), UINT64_C(0), UINT64_C(0));
    check_operation(UINT64_C(0), UINT64_C(31743), UINT64_C(14336), UINT64_C(0),
                    UINT64_C(31744), UINT64_C(5), UINT64_C(1));
    check_operation(UINT64_C(0), UINT64_C(1), UINT64_C(16384), UINT64_C(0),
                    UINT64_C(0), UINT64_C(3), UINT64_C(1));

    check_operation(UINT64_C(1), UINT64_C(15360), UINT64_C(0), UINT64_C(0),
                    UINT64_C(15360), UINT64_C(0), UINT64_C(1));
    check_operation(UINT64_C(1), UINT64_C(17408), UINT64_C(0), UINT64_C(0),
                    UINT64_C(16384), UINT64_C(0), UINT64_C(1));
    check_operation(UINT64_C(1), UINT64_C(16384), UINT64_C(0), UINT64_C(0),
                    UINT64_C(15784), UINT64_C(1), UINT64_C(1));
    check_operation(UINT64_C(1), UINT64_C(16384), UINT64_C(0), UINT64_C(3),
                    UINT64_C(15785), UINT64_C(1), UINT64_C(1));
    check_operation(UINT64_C(1), UINT64_C(48128), UINT64_C(0), UINT64_C(0),
                    UINT64_C(32256), UINT64_C(16), UINT64_C(0));
    check_operation(UINT64_C(1), UINT64_C(32768), UINT64_C(0), UINT64_C(0),
                    UINT64_C(32768), UINT64_C(0), UINT64_C(0));
    check_operation(UINT64_C(1), UINT64_C(31744), UINT64_C(0), UINT64_C(0),
                    UINT64_C(31744), UINT64_C(0), UINT64_C(0));
    check_operation(UINT64_C(1), UINT64_C(32000), UINT64_C(0), UINT64_C(0),
                    UINT64_C(32256), UINT64_C(16), UINT64_C(0));
    check_operation(UINT64_C(1), UINT64_C(1), UINT64_C(0), UINT64_C(0),
                    UINT64_C(3072), UINT64_C(0), UINT64_C(1));
  });
}
