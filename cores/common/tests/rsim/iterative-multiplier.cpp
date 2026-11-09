// Checks captured operands, preparation latency, reset, signed products, and
// response backpressure.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void issue(std::uint8_t left, std::uint8_t right, bool left_signed,
           bool right_signed) {
  while (!request_out.pready)
    tick_model();
  request_in = {.pvalid = UINT64_C(1),
                .pbits = {left, right, {left_signed, right_signed}}};
  tick_model();
  request_in.pvalid = UINT64_C(0);
  request_in.pbits = {};
}

void expect_product(std::uint16_t expected) {
  for (unsigned repeat_index = 0; repeat_index < (9); ++repeat_index) {
    CHECK(!response_out.pvalid);
    CHECK(!request_out.pready);
    tick_model();
  }
  CHECK(response_out.pvalid && response_out.pbits == expected);
}

int main() {
  return run_test([] {
    reset = UINT64_C(1);
    request_in = {};
    response_in = {.pready = UINT64_C(1)};
    tick_model();
    reset = UINT64_C(0);

    issue(UINT64_C(7), UINT64_C(9), UINT64_C(0), UINT64_C(0));
    expect_product(UINT64_C(63));

    issue(UINT64_C(255), UINT64_C(255), UINT64_C(0), UINT64_C(0));
    expect_product(UINT64_C(65025));

    issue(UINT64_C(253), UINT64_C(7), UINT64_C(1), UINT64_C(0));
    expect_product(UINT64_C(65515));

    issue(UINT64_C(128), UINT64_C(255), UINT64_C(1), UINT64_C(1));
    response_in.pready = UINT64_C(0);
    eval();
    expect_product(UINT64_C(128));
    for (unsigned repeat_index = 0; repeat_index < (3); ++repeat_index) {
      tick_model();
      CHECK(response_out.pvalid && response_out.pbits == UINT64_C(128));
      CHECK(!request_out.pready);
    }

    response_in.pready = UINT64_C(1);
    eval();
    CHECK(request_out.pready);
    request_in = {
        .pvalid = UINT64_C(1),
        .pbits = {UINT64_C(255), UINT64_C(254), {UINT64_C(0), UINT64_C(1)}}};
    tick_model();
    request_in.pvalid = UINT64_C(0);
    CHECK(!response_out.pvalid);
    expect_product(UINT64_C(65026));

    issue(UINT64_C(128), UINT64_C(128), UINT64_C(1), UINT64_C(1));
    expect_product(UINT64_C(16384));
    issue(UINT64_C(0), UINT64_C(128), UINT64_C(0), UINT64_C(1));
    expect_product(UINT64_C(0));

    issue(UINT64_C(129), UINT64_C(127), UINT64_C(1), UINT64_C(1));
    reset = UINT64_C(1);
    tick_model();
    reset = UINT64_C(0);
    for (unsigned repeat_index = 0; repeat_index < (10); ++repeat_index) {
      CHECK(!response_out.pvalid && request_out.pready);
      tick_model();
    }
    issue(UINT64_C(255), UINT64_C(255), UINT64_C(1), UINT64_C(1));
    expect_product(UINT64_C(1));

    tick_model();
    CHECK(!response_out.pvalid && request_out.pready);
  });
}
