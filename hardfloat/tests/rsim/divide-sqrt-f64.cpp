// Checks completed multiply-assisted binary64 HardFloat division and
// square-root behavior.
// SPDX-License-Identifier: BSD-3-Clause
#include "flags.hpp"
#include "test.hpp"

void check_operation(bool next_square_root, std::uint64_t next_a,
                     std::uint64_t next_b, std::uint8_t next_rounding_mode,
                     std::uint64_t expected_out, std::uint8_t expected_flags) {
  int elapsed_cycles;
  bool selected_ready;
  bool selected_valid;
  {
    selected_ready = next_square_root ? square_root_ready : division_ready;
    while (!selected_ready) {
      tick_model();
      selected_ready = next_square_root ? square_root_ready : division_ready;
    }
    eval();
    square_root = next_square_root;
    a = next_a;
    b = next_b;
    rounding_mode = next_rounding_mode;
    input_valid = UINT64_C(1);
    tick_model();
    input_valid = UINT64_C(0);
    elapsed_cycles = 0;
    selected_valid = next_square_root ? square_root_valid : division_valid;
    while (!selected_valid && (elapsed_cycles < 40)) {
      tick_model();
      elapsed_cycles = elapsed_cycles + 1;
      selected_valid = next_square_root ? square_root_valid : division_valid;
    }
    if (!selected_valid)
      fail(1, "binary64 divide/sqrt timed out: sqrt=%b a=%h b=%h",
           next_square_root, next_a, next_b);
    if ((out != expected_out) || (flag_bits(exception_flags) != expected_flags))
      fail(1,
           "binary64 divide/sqrt failed: sqrt=%b a=%h b=%h mode=%h output=%h "
           "flags=%h expected=%h/%h",
           next_square_root, next_a, next_b, next_rounding_mode, out,
           flag_bits(exception_flags), expected_out, expected_flags);
    if (next_square_root && division_valid)
      fail(1, "binary64 square root asserted a division completion");
    if (!next_square_root && square_root_valid)
      fail(1, "binary64 division asserted a square-root completion");
  }
}

void check_division_pipeline() {
  int result_count;
  int wait_cycles;
  bool first_completed_before_second_start;
  {
    while (!division_ready)
      tick_model();
    eval();
    square_root = UINT64_C(0);
    a = UINT64_C(4620693217682128896);
    b = UINT64_C(4611686018427387904);
    rounding_mode = UINT64_C(0);
    input_valid = UINT64_C(1);
    tick_model();
    input_valid = UINT64_C(0);
    first_completed_before_second_start = division_valid;

    wait_cycles = 0;
    while (!division_ready && (wait_cycles < 6)) {
      tick_model();
      wait_cycles = wait_cycles + 1;
      first_completed_before_second_start =
          first_completed_before_second_start || division_valid;
    }
    if (!division_ready)
      fail(1, "binary64 division pipeline did not reopen within six cycles");
    if (first_completed_before_second_start)
      fail(1,
           "binary64 first division completed before the second was admitted");

    eval();
    a = UINT64_C(4621256167635550208);
    b = UINT64_C(4613937818241073152);
    input_valid = UINT64_C(1);
    tick_model();
    input_valid = UINT64_C(0);

    result_count = 0;
    wait_cycles = 0;
    while ((result_count < 2) && (wait_cycles < 50)) {
      if (division_valid) {
        if ((result_count == 0) &&
            ((out != UINT64_C(4616189618054758400)) ||
             (flag_bits(exception_flags) != UINT64_C(0))))
          fail(1,
               "first pipelined binary64 division was not returned first: "
               "output=%h flags=%h",
               out, flag_bits(exception_flags));
        if ((result_count == 1) &&
            ((out != UINT64_C(4613937818241073152)) ||
             (flag_bits(exception_flags) != UINT64_C(0))))
          fail(1,
               "second pipelined binary64 division was incorrect: output=%h "
               "flags=%h",
               out, flag_bits(exception_flags));
        result_count = result_count + 1;
      }
      if (square_root_valid)
        fail(1, "pipelined divisions asserted a square-root completion");
      tick_model();
      wait_cycles = wait_cycles + 1;
    }
    if (result_count != 2)
      fail(1, "binary64 division pipeline did not return both results");
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

    check_operation(UINT64_C(0), UINT64_C(4607182418800017408),
                    UINT64_C(4611686018427387904), UINT64_C(0),
                    UINT64_C(4602678819172646912), UINT64_C(0));
    check_operation(UINT64_C(0), UINT64_C(4613937818241073152),
                    UINT64_C(4611686018427387904), UINT64_C(0),
                    UINT64_C(4609434218613702656), UINT64_C(0));
    check_operation(UINT64_C(0), UINT64_C(13830554455654793216),
                    UINT64_C(4611686018427387904), UINT64_C(0),
                    UINT64_C(13826050856027422720), UINT64_C(0));
    check_operation(UINT64_C(0), UINT64_C(4607182418800017408),
                    UINT64_C(4613937818241073152), UINT64_C(0),
                    UINT64_C(4599676419421066581), UINT64_C(1));
    check_operation(UINT64_C(0), UINT64_C(4607182418800017408),
                    UINT64_C(4613937818241073152), UINT64_C(3),
                    UINT64_C(4599676419421066582), UINT64_C(1));
    check_operation(UINT64_C(0), UINT64_C(0), UINT64_C(0), UINT64_C(0),
                    UINT64_C(9221120237041090560), UINT64_C(16));
    check_operation(UINT64_C(0), UINT64_C(4607182418800017408), UINT64_C(0),
                    UINT64_C(0), UINT64_C(9218868437227405312), UINT64_C(8));
    check_operation(UINT64_C(0), UINT64_C(9218868437227405311),
                    UINT64_C(4602678819172646912), UINT64_C(0),
                    UINT64_C(9218868437227405312), UINT64_C(5));
    check_operation(UINT64_C(0), UINT64_C(1), UINT64_C(4611686018427387904),
                    UINT64_C(0), UINT64_C(0), UINT64_C(3));

    check_operation(UINT64_C(1), UINT64_C(0), UINT64_C(4607182418800017408),
                    UINT64_C(0), UINT64_C(4607182418800017408), UINT64_C(0));
    check_operation(UINT64_C(1), UINT64_C(0), UINT64_C(4616189618054758400),
                    UINT64_C(0), UINT64_C(4611686018427387904), UINT64_C(0));
    check_operation(UINT64_C(1), UINT64_C(0), UINT64_C(4611686018427387904),
                    UINT64_C(0), UINT64_C(4609047870845172685), UINT64_C(1));
    check_operation(UINT64_C(1), UINT64_C(0), UINT64_C(4611686018427387904),
                    UINT64_C(2), UINT64_C(4609047870845172684), UINT64_C(1));
    check_operation(UINT64_C(1), UINT64_C(0), UINT64_C(13830554455654793216),
                    UINT64_C(0), UINT64_C(9221120237041090560), UINT64_C(16));
    check_operation(UINT64_C(1), UINT64_C(0), UINT64_C(9223372036854775808),
                    UINT64_C(0), UINT64_C(9223372036854775808), UINT64_C(0));
    check_operation(UINT64_C(1), UINT64_C(0), UINT64_C(9218868437227405312),
                    UINT64_C(0), UINT64_C(9218868437227405312), UINT64_C(0));
    check_operation(UINT64_C(1), UINT64_C(0), UINT64_C(9219994337134247936),
                    UINT64_C(0), UINT64_C(9221120237041090560), UINT64_C(16));
    check_operation(UINT64_C(1), UINT64_C(0), UINT64_C(1), UINT64_C(0),
                    UINT64_C(2188749418902061056), UINT64_C(0));

    check_division_pipeline();
  });
}
