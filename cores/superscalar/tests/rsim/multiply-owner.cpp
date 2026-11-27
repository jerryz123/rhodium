// SPDX-License-Identifier: Apache-2.0
#include "wide.hpp"

unsigned scalar_launches = 0, vector_launches = 0;
extern "C" void observe_multiply_launch(std::uint64_t resetting, std::uint64_t valid,
                                        std::uint64_t is_vector) {
  if (!resetting && valid) ++(is_vector ? vector_launches : scalar_launches);
}

int main() {
  return run_test([] {
    const auto step = [] { eval(); tick_model(); eval(); };
    reset = 1;
    step();
    reset = 0;

    book_in.pvalid = 1;
    step();
    // Recovery vetoes the booked scalar's EX issue. Even with a vector waiting,
    // this cycle is a bubble; stale scalar bits do not start an operation.
    book_in.pvalid = 0;
    issue_in.pbits.poperands.pleft = 101;
    issue_in.pbits.poperands.pright = 111;
    vector_request_in.pvalid = 1;
    auto &request = vector_request_in.pbits;
    request.poperands.pleft = UINT64_MAX - 1; // signed -2 times unsigned 5
    request.poperands.pright = 5;
    request.poperands.pmode.pleft_usigned = 1;
    request.ptag.pslot = 3;
    request.ptag.psew = 2;
    request.ptag.pselect = 1;
    request.ptag.prounding_umode = 2;
    eval();
    CHECK(!vector_request_out.pready && drained);
    step();

    // The vector now fires and RR books the immediately following scalar slot.
    CHECK(!scalar_launches && !vector_launches);
    CHECK(vector_request_out.pready && drained);
    book_in.pvalid = 1;
    step();
    CHECK(!scalar_launches && vector_launches == 1);
    book_in.pvalid = 0;
    vector_request_in = {};
    issue_in.pvalid = 1;
    issue_in.pbits.poperands.pleft = 6;
    issue_in.pbits.poperands.pright = 7;
    eval();
    CHECK(!vector_request_out.pready && !completed_out.pvalid);
    step();
    CHECK(scalar_launches == 1 && vector_launches == 1);
    issue_in = {};
    CHECK(!drained && !completed_out.pvalid && !vector_result_out.pvalid);
    step();
    CHECK(!completed_out.pvalid && !vector_result_out.pvalid);
    authorize_in.pvalid = 1;
    authorize_in.pbits.prd = 9;
    step();
    authorize_in.pvalid = 0;
    CHECK(completed_out.pvalid && completed_out.pbits.prd == 9);
    CHECK(completed_out.pbits.pdata == 42);

    // Hold the older vector return across the scalar completion and handoff.
    for (unsigned stall = 0; stall < 3; ++stall) {
      const auto &result = vector_result_out.pbits;
      CHECK(vector_result_out.pvalid && wide_value(result.pvalue) == uint128(0) - 10);
      CHECK(result.ptag.pslot == 3 && result.ptag.psew == 2);
      CHECK(result.ptag.pselect == 1 && result.ptag.prounding_umode == 2);
      step();
      CHECK(drained && !completed_out.pvalid);
    }
    vector_result_in.pready = 1;
    step();
    CHECK(!vector_result_out.pvalid && !completed_out.pvalid);
    CHECK(scalar_launches == 1 && vector_launches == 1);
  });
}
