// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include <deque>

using Request = std::remove_cvref_t<decltype(vector_request_in.pbits)>;
using ScalarRequest = std::remove_cvref_t<decltype(fixed_issue_in.pbits)>;
constexpr std::uint64_t three = 0x4008000000000000ULL;

Request addition(unsigned tag) {
  Request request{};
  request.ptag = tag;
  request.pcontrol.pregisters.pdestination = 2;
  request.pcontrol.pregisters.puses_ufrs1 = 1;
  request.pcontrol.pregisters.puses_ufrs2 = 1;
  request.pcontrol.pexecution.punit = 2; // Add
  request.pcontrol.pexecution.psource_uprecision = 2;
  request.pcontrol.pexecution.pdestination_uprecision = 2;
  request.pleft_uprecision = request.pright_uprecision = request.pthird_uprecision = 2;
  request.pleft = 0x3ff0000000000000ULL; // 1.0
  request.pright = 0x4000000000000000ULL; // 2.0
  return request;
}

void initialize() {
  dut = Model{};
  vector_result_in.pready = 1;
  reset = 1;
  tick_model();
  reset = 0;
  eval();
}

void check_result(unsigned tag) {
  CHECK(vector_result_out.pvalid);
  CHECK(vector_result_out.pbits.ptag == tag);
  CHECK(vector_result_out.pbits.pvalue == three);
  CHECK(vector_result_out.pbits.pexception_uflags == 0);
}

void fixed_returns(bool serial) {
  initialize();
  struct Pending { unsigned tag, due; };
  std::deque<Pending> pending;
  unsigned sent = 0, received = 0, first = 0, last = 0;
  for (unsigned cycle = 0; cycle < 500 && received < 100; ++cycle) {
    vector_request_in.pvalid = sent < 100 && (!serial || pending.empty());
    vector_request_in.pbits = addition(sent % 8);
    eval();
    const bool accepted = vector_request_in.pvalid && vector_request_out.pready;
    const bool due = !pending.empty() && pending.front().due == cycle;
    CHECK(bool(vector_result_out.pvalid) == due);
    if (due) {
      check_result(pending.front().tag);
      pending.pop_front();
      last = cycle;
      ++received;
    }
    if (accepted) {
      if (!sent) first = cycle;
      pending.push_back({sent % 8, cycle + 2});
      ++sent;
    }
    tick_model();
  }
  CHECK(sent == 100 && received == 100 && pending.empty());
  CHECK(last - first == (serial ? 299 : 101));
}

void variable_backpressure() {
  initialize();
  auto divide = addition(6);
  divide.pcontrol.pexecution.punit = 4; // DivideSquareRoot
  divide.pleft = 0x4018000000000000ULL; // 6.0 / 2.0 = 3.0
  vector_request_in.pvalid = 1;
  vector_request_in.pbits = divide;
  eval();
  CHECK(vector_request_out.pready);
  tick_model();
  vector_request_in.pvalid = 0;
  vector_result_in.pready = 0;
  unsigned wait = 0;
  for (; wait < 100; ++wait) {
    eval();
    if (vector_result_out.pvalid) break;
    tick_model();
  }
  CHECK(wait < 100);
  check_result(6);

  // A promised fixed return wins over a blocked divide without consuming it.
  vector_request_in.pvalid = 1;
  vector_request_in.pbits = addition(3);
  eval();
  CHECK(vector_request_out.pready);
  tick_model();
  vector_request_in.pvalid = 0;
  eval();
  check_result(6);
  tick_model();
  vector_result_in.pready = 1;
  eval();
  check_result(3);
  tick_model();

  // The original variable owner still holds its result until transfer.
  vector_result_in.pready = 0;
  for (unsigned cycle = 0; cycle < 5; ++cycle) {
    eval();
    check_result(6);
    tick_model();
  }
  vector_result_in.pready = 1;
  eval();
  check_result(6);
  tick_model();
  CHECK(!vector_result_out.pvalid);
}

ScalarRequest scalar_add(unsigned rd) {
  const auto vector = addition(0);
  ScalarRequest request{};
  request.pcontrol = vector.pcontrol;
  request.pleft = vector.pleft;
  request.pright = vector.pright;
  request.pleft_uprecision = request.pright_uprecision = request.pthird_uprecision = 2;
  request.ptag.prd = rd;
  request.ptag.pdestination = 2;
  request.ptag.pspeculative = 1;
  request.ptag.pdirect = 1;
  request.ptag.pidentity.ppc = UINT64_C(0xfffff00000000000) + 4 * rd;
  request.ptag.pidentity.praw_uinstruction = 0x02000053 | (rd << 7);
  return request;
}

void check_scalar_identity(const ScalarRequest &request) {
  const auto &actual = completion_out.pbits.ptag;
  CHECK(actual.pidentity.ppc == request.ptag.pidentity.ppc);
  CHECK(actual.pidentity.praw_uinstruction == request.ptag.pidentity.praw_uinstruction);
  CHECK(actual.prd == request.ptag.prd && actual.plane == request.ptag.plane);
  CHECK(actual.pdestination == request.ptag.pdestination);
  CHECK(actual.pspeculative == request.ptag.pspeculative && actual.pdirect == request.ptag.pdirect);
}

void direct_fpr_and_loads() {
  initialize();
  vector_reserve_in.pvalid = load_reserve_in.pvalid = 1;
  vector_reserve_in.pbits = 3;
  load_reserve_in.pbits.prd = 5;
  vector_writeback_in.pvalid = 1;
  vector_writeback_in.pbits = 2;
  eval();
  CHECK(vector_writeback_out.pready);
  tick_model();
  vector_reserve_in.pvalid = load_reserve_in.pvalid = vector_writeback_in.pvalid = 0;
  tick_model();

  read_addresses = {3, 4, 5};
  vector_write_in.pvalid = load_hit_in.pvalid = load_return_in.pvalid = 1;
  vector_write_in.pbits.paddress = 3;
  vector_write_in.pbits.pdata = three;
  load_hit_in.pbits.powner.prd = 4;
  load_hit_in.pbits.pprecision = 2;
  load_hit_in.pbits.pdata = three + (UINT64_C(1) << 52);
  load_return_in.pbits.powner.prd = 5;
  load_return_in.pbits.pprecision = 2;
  load_return_in.pbits.pdata = three;
  eval();
  CHECK(vector_written_out.pvalid && read_values[0] == three);
  CHECK(load_write_out.pvalid && read_values[1] == load_hit_in.pbits.pdata);
  CHECK(!load_return_out.pready && !(busy & (1u << 3)));
  tick_model();
  vector_write_in.pvalid = load_hit_in.pvalid = 0;
  eval();
  CHECK(load_return_out.pready && read_values[2] == three);
  tick_model();
  load_return_in.pvalid = 0;
  eval();
  CHECK(drained && !busy && !vector_written_out.pvalid);
}

void fpr_calendar_collisions() {
  initialize();
  const auto scalar = scalar_add(3);
  probe = scalar.pcontrol;
  vector_reserve_in.pvalid = vector_writeback_in.pvalid = 1;
  vector_reserve_in.pbits = 5;
  vector_writeback_in.pbits = 3;
  book_in.pvalid = 1;
  book_in.pbits = scalar.pcontrol;
  eval();
  CHECK(available && !vector_writeback_out.pready); // Same-edge RR booking wins.
  book_in.pvalid = 0;
  eval();
  CHECK(vector_writeback_out.pready);
  tick_model();

  vector_reserve_in.pvalid = vector_writeback_in.pvalid = 0;
  book_in.pvalid = 1;
  book_in.pbits = scalar.pcontrol;
  eval();
  CHECK(available);
  tick_model();

  book_in.pvalid = 0;
  fixed_issue_in.pvalid = 1;
  fixed_issue_in.pbits = scalar;
  vector_writeback_in.pvalid = 1;
  vector_writeback_in.pbits = 2;
  eval();
  CHECK(!vector_writeback_out.pready); // Scalar already owns cycle 4.
  tick_model();

  fixed_issue_in.pvalid = 0;
  vector_write_in.pvalid = vector_reserve_in.pvalid = 1;
  vector_write_in.pbits.paddress = vector_reserve_in.pbits = 5;
  vector_write_in.pbits.pdata = three;
  eval();
  CHECK(vector_writeback_out.pready && vector_written_out.pvalid);
  tick_model(); // Return old f5 and reserve its replacement on the same edge.
  vector_writeback_in.pvalid = vector_write_in.pvalid = vector_reserve_in.pvalid = 0;
  authorize_in.pvalid = 1;
  authorize_in.pbits = scalar;
  eval();
  CHECK((busy & (1u << 5)) && completion_out.pvalid);
  CHECK(completion_out.pbits.pvalue == three);
  check_scalar_identity(scalar);
  tick_model();

  authorize_in.pvalid = 0;
  vector_write_in.pvalid = 1;
  vector_write_in.pbits.pdata = three + (UINT64_C(1) << 52);
  read_addresses = {3, 5, 0};
  eval();
  CHECK(vector_written_out.pvalid && !completion_out.pvalid);
  CHECK(read_values[0] == three && read_values[1] == vector_write_in.pbits.pdata);
  tick_model();
  vector_write_in.pvalid = 0;
  eval();
  CHECK(drained && !busy);
}

unsigned scalar_divide_latency() {
  initialize();
  auto divide = scalar_add(4);
  divide.ptag.pspeculative = 0;
  divide.pcontrol.pexecution.punit = 4;
  divide.pleft = 0x4018000000000000ULL;
  variable_issue_in.pvalid = 1;
  variable_issue_in.pbits = divide;
  eval();
  CHECK(variable_issue_out.pready);
  tick_model();
  variable_issue_in.pvalid = 0;
  for (unsigned cycle = 1; cycle < 200; ++cycle) {
    eval();
    if (completion_out.pvalid) {
      CHECK(completion_out.pbits.pvalue == three);
      check_scalar_identity(divide);
      tick_model();
      return cycle;
    }
    tick_model();
  }
  throw std::runtime_error("scalar divide did not complete");
}

void variable_fpr_collision(unsigned due) {
  initialize();
  CHECK(due > 2);
  auto divide = scalar_add(4);
  divide.ptag.pspeculative = 0;
  divide.pcontrol.pexecution.punit = 4;
  divide.pleft = 0x4018000000000000ULL;
  variable_issue_in.pvalid = vector_reserve_in.pvalid = 1;
  variable_issue_in.pbits = divide;
  vector_reserve_in.pbits = 5;
  eval();
  CHECK(variable_issue_out.pready);
  tick_model();
  variable_issue_in.pvalid = vector_reserve_in.pvalid = 0;
  for (unsigned cycle = 1; cycle <= due + 1; ++cycle) {
    vector_writeback_in.pvalid = cycle == due - 2;
    vector_writeback_in.pbits = 2;
    vector_write_in.pvalid = cycle == due;
    vector_write_in.pbits.paddress = 5;
    vector_write_in.pbits.pdata = three;
    eval();
    if (vector_writeback_in.pvalid) CHECK(vector_writeback_out.pready);
    CHECK(bool(completion_out.pvalid) == (cycle == due + 1));
    CHECK(bool(vector_written_out.pvalid) == (cycle == due));
    if (completion_out.pvalid) {
      CHECK(completion_out.pbits.pvalue == three);
      check_scalar_identity(divide);
    }
    tick_model();
  }
  CHECK(drained && !busy);
}

void compact_variable_owners() {
  for (const auto pc : {UINT64_C(0x00000ffffffffffe),
                        UINT64_C(0xfffff00000000000), UINT64_MAX - 1}) {
    initialize();
    auto request = scalar_add(7);
    request.ptag.pspeculative = 0;
    request.ptag.pidentity.ppc = pc;
    request.pcontrol.pexecution.punit = 4;
    request.pleft = 0x4018000000000000ULL;
    variable_issue_in.pvalid = 1;
    variable_issue_in.pbits = request;
    eval();
    CHECK(variable_issue_out.pready);
    tick_model();
    variable_issue_in.pvalid = 0;
    // Live input changes must not replace the admitted owner.
    variable_issue_in.pbits.ptag.pidentity = {};
    unsigned cycle = 0;
    for (; cycle < 200 && !completion_out.pvalid; ++cycle) tick_model();
    CHECK(cycle < 200 && completion_out.pbits.pvalue == three);
    check_scalar_identity(request);
    tick_model();
    CHECK(drained);
  }
  expect_failure("superscalar_fp_variable_owner_pc_fits", [] {
    initialize();
    auto request = scalar_add(7);
    request.ptag.pspeculative = 0;
    request.ptag.pidentity.ppc = UINT64_C(1) << 44;
    request.pcontrol.pexecution.punit = 4;
    variable_issue_in.pvalid = 1;
    variable_issue_in.pbits = request;
    tick_model();
  });
}

int main() {
  return run_test([] {
    fixed_returns(false);
    fixed_returns(true);
    variable_backpressure();
    direct_fpr_and_loads();
    fpr_calendar_collisions();
    variable_fpr_collision(scalar_divide_latency());
    compact_variable_owners();
  });
}
