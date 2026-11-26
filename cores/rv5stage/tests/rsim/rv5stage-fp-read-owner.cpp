// Checks the scheduled scalar adapter's read ownership across cancellation and WB handoff.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

void load_hit(unsigned rd, std::uint64_t data) {
  load_hit_in.pvalid = 1;
  load_hit_in.pbits.prd = rd;
  load_hit_in.pbits.pprecision = 2; // D
  load_hit_in.pbits.pdata = data;
}

int main() {
  return run_test([] {
    constexpr std::uint64_t ex_value = 0x3ff0000000000000, dividend = 0x4010000000000000;
    constexpr std::uint64_t snapshot = 0x4008000000000000;
    reset = 1;
    tick_model();
    reset = 0;
    execution_request_in.pready = 1;
    wb_read_address = 5;
    load_hit(3, ex_value);
    tick_model();
    load_hit(5, 0);
    tick_model();
    load_hit(7, 0);
    tick_model();
    load_hit_in = {};

    // WB offers a divide. Cancel the booked EX launch before the edge: the
    // read still names f3, but neither requester can take the execution port.
    issue_in.pvalid = 1;
    issue_in.pbits.pfrs1 = 5;
    issue_in.pbits.pfrd = 9;
    issue_in.pbits.pcontrol.pexecution.punit = 4; // divide/sqrt
    issue_in.pbits.pcontrol.pregisters.puses_ufrs1 = 1;
    issue_in.pbits.pcontrol.pregisters.pdestination = 2; // FPR
    fixed_reserved = 1;
    fixed_issue_in.pvalid = 1;
    fixed_issue_in.pbits.pfrs1 = 3;
    fixed_issue_in.pbits.pcontrol.pexecution.punit = 2; // fixed add
    fixed_issue_in.pbits.pcontrol.pregisters.puses_ufrs1 = 1;
    eval();
    CHECK(execution_request_out.pvalid && execution_request_out.pbits.pleft == ex_value);
    fixed_issue_in.pvalid = 0;
    eval();
    CHECK(snapshot_read_data == ex_value && !execution_request_out.pvalid && !issue_out.pready);
    tick_model();
    eval();
    CHECK(drained && busy == 0);

    // Release to WB. A simultaneous write to f5 must reach the divide before
    // storage updates; only the accepted divide acquires a destination.
    fixed_reserved = 0;
    load_hit(5, dividend);
    eval();
    CHECK(issue_out.pready && execution_request_out.pvalid);
    CHECK(execution_request_out.pbits.pleft == dividend && snapshot_read_data == dividend);
    const auto divide_tag = execution_request_out.pbits.ptag;
    tick_model();
    issue_in.pvalid = 0;
    wb_read_address = 7;
    load_hit(7, snapshot);
    eval();
    CHECK(busy == (1u << 9) && !execution_request_out.pvalid && snapshot_read_data == snapshot);
    tick_model();
    load_hit_in = {};
    eval();
    CHECK(snapshot_read_data == snapshot && !execution_request_out.pvalid);

    // The other FPR write port forwards the returning divide to a snapshot on
    // its completion edge, before the old reservation clears.
    wb_read_address = 9;
    completion_in.pready = 1;
    execution_result_in.pvalid = 1;
    execution_result_in.pbits.ptag = divide_tag;
    execution_result_in.pbits.pvalue = ex_value;
    eval();
    CHECK(execution_result_out.pready && completion_out.pvalid && snapshot_read_data == ex_value);
    tick_model();
    execution_result_in.pvalid = 0;
    eval();
    CHECK(drained && busy == 0 && snapshot_read_data == ex_value);
  });
}
