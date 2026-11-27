// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = 1; tick_model(); reset = 0;
    auto fill = [](unsigned row, unsigned value) {
      fill_in.pvalid = 1;
      fill_in.pbits.prow_unumber = row;
      fill_in.pbits.pmask = 0xffff;
      fill_in.pbits.pdata.words[0] = value;
    };
    auto lookup = [](auto &port, unsigned row, bool data = true) {
      port.pvalid = 1;
      port.pbits.prow = row;
      port.pbits.pwith_udata = data;
    };
    for (unsigned i = 0; i < 8; ++i) {
      fill(i, 0x100+i);
      eval(); CHECK(fill_out.pready); tick_model();
    }
    // Three independent row operations plus a lookup on the fourth bank.
    gather_in.pvalid = victim_in.pvalid = 1;
    gather_in.pbits = 0; victim_in.pbits = 1;
    fill(2, 0x202); lookup(demand_in, 3);
    eval();
    CHECK(gather_out.pready && victim_out.pready && fill_out.pready && demand_out.pready);
    tick_model(); eval();
    CHECK(snoop_data_out.pvalid && snoop_data_out.pbits[0].words[0] == 0x100);
    CHECK(victim_data_out.pvalid && victim_data_out.pbits[0].words[0] == 0x101);
    CHECK(spec_result_out.pvalid && spec_result_out.pbits.prows[0].words[0] == 0x103);
    // Collide on row bank only. The denied lookup must not consume its
    // metadata bank: a younger metadata-only command to the same set fits.
    gather_in.pbits = 0; victim_in.pvalid = fill_in.pvalid = 0;
    lookup(retry_in, 4); lookup(demand_in, 5, false);
    eval(); CHECK(!retry_out.pready && demand_out.pready);
    tick_model(); eval();
    CHECK(!service_result_out.pvalid && spec_result_out.pvalid && !spec_result_out.pbits.pwith_udata);
    // Retry and younger issue share a return lane. Speculation can still use
    // a different metadata and row bank. Check actual data, not just grants.
    gather_in.pvalid = 0; lookup(issue_in, 6); lookup(demand_in, 2);
    eval(); CHECK(retry_out.pready && !issue_out.pready && demand_out.pready);
    tick_model(); eval();
    CHECK(service_result_out.pbits.prows[0].words[0] == 0x104);
    CHECK(spec_result_out.pbits.prows[0].words[0] == 0x202);
    // Publication needs both metadata writes. A probe excludes the entire
    // action; on release the tag and permission appear together.
    demand_in.pvalid = issue_in.pvalid = retry_in.pvalid = 0;
    probe_in.pvalid = publish_in.pvalid = 1;
    probe_in.pbits = publish_in.pbits.pset = 1;
    publish_in.pbits.ptag = 0x123; publish_in.pbits.pstate = 2;
    eval(); CHECK(probe_out.pready && !publish_out.pready); tick_model();
    probe_in.pvalid = 0;
    drain_in.pvalid = 1; drain_in.pbits.prow_unumber = 4;
    drain_in.pbits.pmark_udirty = 1; drain_in.pbits.pmask = 1;
    drain_in.pbits.pdata.words[0] = 0x57;
    eval(); CHECK(publish_out.pready && !drain_out.pready); tick_model();
    publish_in.pvalid = 0; lookup(demand_in, 5, false);
    eval(); CHECK(drain_out.pready && demand_out.pready); tick_model(); eval();
    CHECK(spec_result_out.pbits.ptags[0] == 0x123 && spec_result_out.pbits.pstates[0] == 4);
    drain_in.pvalid = 0; lookup(demand_in, 4);
    tick_model(); eval(); CHECK(spec_result_out.pbits.prows[0].words[0] == 0x157);
  });
}
