// Exercises unsigned, signed, exceptional, and backpressured divider
// transactions.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void issue(std::uint8_t dividend, std::uint8_t divisor, bool signed_mode) {
  while (!request_out.pready)
    tick_model();
  request_in = {.pvalid = UINT64_C(1),
                .pbits = {dividend, divisor, signed_mode}};
  tick_model();
  request_in.pvalid = UINT64_C(0);
}

void expect_result_after(int wait_cycles, std::uint8_t quotient,
                         std::uint8_t remainder) {
  for (unsigned repeat_index = 0; repeat_index < (wait_cycles);
       ++repeat_index) {
    CHECK(!response_out.pvalid);
    CHECK(!request_out.pready);
    tick_model();
  }
  CHECK(response_out.pvalid && response_out.pbits.pquotient == quotient &&
        response_out.pbits.premainder == remainder);
}

int significant_bits(std::uint8_t value) {
  int return_value{};

  return_value = 0;
  for (int index = 0; index < 8; index++) {
    if (((value >> (index)) & 1))
      return_value = index + 1;
  }

  return return_value;
}

void expect_reference(std::uint8_t dividend, std::uint8_t divisor,
                      bool signed_mode) {
  std::int8_t signed_dividend;
  std::int8_t signed_divisor;
  std::uint8_t dividend_magnitude;
  std::uint8_t divisor_magnitude;
  std::uint8_t expected_quotient;
  std::uint8_t expected_remainder;
  int expected_wait;
  int observed_wait;

  signed_dividend = dividend;
  signed_divisor = divisor;
  dividend_magnitude =
      signed_mode && ((dividend >> (7)) & 1) ? -dividend : dividend;
  divisor_magnitude =
      signed_mode && ((divisor >> (7)) & 1) ? -divisor : divisor;
  if (divisor == 0) {
    expected_quotient = UINT64_MAX;
    expected_remainder = dividend;
  } else if (signed_mode && dividend == UINT64_C(128) &&
             divisor == UINT64_C(255)) {
    expected_quotient = UINT64_C(128);
    expected_remainder = UINT64_C(0);
  } else if (signed_mode) {
    expected_quotient = signed_dividend / signed_divisor;
    expected_remainder = signed_dividend % signed_divisor;
  } else {
    expected_quotient = dividend / divisor;
    expected_remainder = dividend % divisor;
  }
  expected_wait = divisor_magnitude == 0 || divisor_magnitude == 1 ||
                          dividend_magnitude <= divisor_magnitude
                      ? 0
                      : significant_bits(dividend_magnitude) + 1;
  observed_wait = 0;
  while (!response_out.pvalid) {
    CHECK(!request_out.pready && observed_wait < 9);
    tick_model();
    observed_wait++;
  }
  CHECK(observed_wait == expected_wait);
  CHECK(response_out.pbits.pquotient == expected_quotient &&
        response_out.pbits.premainder == expected_remainder);
}

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    request_in = {};
    response_in = {.pready = UINT64_C(1)};
    tick_model();
    reset = UINT64_C(0);

    issue(UINT64_C(100), UINT64_C(7), UINT64_C(0));
    expect_result_after(8, UINT64_C(14), UINT64_C(2));

    issue(-UINT64_C(100), UINT64_C(7), UINT64_C(1));
    expect_result_after(8, -UINT64_C(14), -UINT64_C(2));

    issue(UINT64_C(100), -UINT64_C(7), UINT64_C(1));
    expect_result_after(8, -UINT64_C(14), UINT64_C(2));

    issue(UINT64_C(255), UINT64_C(16), UINT64_C(0));
    expect_result_after(9, UINT64_C(15), UINT64_C(15));

    issue(UINT64_C(253), UINT64_C(0), UINT64_C(1));
    expect_result_after(0, UINT64_C(255), UINT64_C(253));

    issue(UINT64_C(3), UINT64_C(7), UINT64_C(0));
    expect_result_after(0, UINT64_C(0), UINT64_C(3));

    issue(-UINT64_C(7), UINT64_C(7), UINT64_C(1));
    expect_result_after(0, -UINT64_C(1), UINT64_C(0));

    issue(UINT64_C(128), UINT64_C(255), UINT64_C(1));
    response_in.pready = UINT64_C(0);
    eval();
    expect_result_after(0, UINT64_C(128), UINT64_C(0));
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
      tick_model();
      CHECK(response_out.pvalid &&
            response_out.pbits.pquotient == UINT64_C(128) &&
            response_out.pbits.premainder == UINT64_C(0));
      CHECK(!request_out.pready);
    }

    response_in.pready = UINT64_C(1);
    eval();
    CHECK(request_out.pready);
    request_in = {.pvalid = UINT64_C(1),
                  .pbits = {UINT64_C(37), UINT64_C(5), UINT64_C(0)}};
    tick_model();
    request_in.pvalid = UINT64_C(0);
    CHECK(!response_out.pvalid);
    expect_result_after(7, UINT64_C(7), UINT64_C(2));

    for (int signed_mode = 0; signed_mode < 2; signed_mode++) {
      for (int dividend = 0; dividend < 256; dividend++) {
        for (int divisor = 0; divisor < 256; divisor++) {
          issue(slice(dividend, 7, 0), slice(divisor, 7, 0),
                ((signed_mode >> (0)) & 1));
          expect_reference(slice(dividend, 7, 0), slice(divisor, 7, 0),
                           ((signed_mode >> (0)) & 1));
        }
      }
    }

    tick_model();
    CHECK(!response_out.pvalid && request_out.pready);
  });
}
