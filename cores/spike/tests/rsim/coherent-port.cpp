// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "test-sites.h"
#include <map>
#include <set>

int main() {
  return run_test([] {
    auto& graph = rheg::graph();
    graph.bind_manifest(rheg_generated::manifest());
    reset = 1;
    tick_model();
    reset = 0;
    std::map<unsigned, unsigned> sequence;
    std::set<std::pair<rheg::Ref, rheg::Ref>> expected;
    auto transfer = [&](unsigned from, unsigned to) {
      const rheg::Ref parent{from, sequence[from]++}, child{to, sequence[to]++};
      CHECK(graph.nodes.at(child).cycle == graph.nodes.at(parent).cycle);
      CHECK(!graph.nodes.at(child).ancestry_unknown);
      expected.emplace(parent, child);
    };
    auto check_graph = [&] {
      CHECK(graph.edges == expected && graph.nodes.size() == 2 * expected.size());
    };

    auto check_merge = [&](auto& high, const auto& high_ready, auto& low, const auto& low_ready,
                           auto& sink, const auto& output, unsigned high_site, unsigned low_site,
                           unsigned sink_site) {
      high.pbits.ptxn_uid = 17;
      low.pbits.ptxn_uid = 42;
      auto cycle = [&](bool preferred, bool fallback, bool ready) {
        high.pvalid = preferred;
        low.pvalid = fallback;
        sink.pready = ready;
        eval();
        CHECK(bool(high_ready.pready) == ready);
        CHECK(bool(low_ready.pready) == (ready && !preferred));
        CHECK(bool(output.pvalid) == (preferred || fallback));
        CHECK(output.pbits.ptxn_uid == (preferred ? 17 : 42));
        tick_model();
        if (ready && (preferred || fallback)) transfer(preferred ? high_site : low_site, sink_site);
        check_graph();
      };
      cycle(false, false, false);
      cycle(false, false, true);
      cycle(false, true, false);  // Low is offered first; high preempts during the stall.
      cycle(true, true, false);
      cycle(true, true, false);
      cycle(true, true, true);
      cycle(false, true, true);   // The pending low offer now transfers.
      cycle(true, false, true);
      cycle(false, false, true);
    };
    check_merge(release_req_in, release_req_out, acquire_req_in, acquire_req_out,
                chi_in.prequests, chi_out.prequests, site_release_req, site_acquire_req, site_req);
    check_merge(snoop_rsp_in, snoop_rsp_out, acquire_ack_in, acquire_ack_out,
                chi_in.prequester_uresponses, chi_out.prequester_uresponses, site_snoop_rsp, site_acquire_ack, site_rsp);
    check_merge(snoop_dat_in, snoop_dat_out, release_dat_in, release_dat_out,
                chi_in.prequest_udata, chi_out.prequest_udata, site_snoop_dat, site_release_dat, site_dat);

    auto response = [&](unsigned txn, bool valid, bool acquire_ready, bool release_ready) {
      chi_in.presponses.pbits.ptxn_uid = txn;
      chi_in.presponses.pvalid = valid;
      acquire_rsp_in.pready = acquire_ready;
      release_rsp_in.pready = release_ready;
      eval();
      const bool release = txn == 1;
      const bool ready = release ? release_ready : acquire_ready;
      CHECK(bool(chi_out.presponses.pready) == ready);
      CHECK(bool(acquire_rsp_out.pvalid) == (valid && !release));
      CHECK(bool(release_rsp_out.pvalid) == (valid && release));
      CHECK(acquire_rsp_out.pbits.ptxn_uid == txn && release_rsp_out.pbits.ptxn_uid == txn);
      tick_model();
      if (valid && ready) transfer(site_rxrsp, release ? site_release_rsp : site_acquire_rsp);
      check_graph();
    };
    response(0, false, true, false);
    response(0, true, false, true);
    response(0, true, true, false);
    response(1, true, true, false);
    response(1, true, false, true);
    response(3, true, true, false); // Only the full TxnID 1 selects release.
    response(1, false, false, true);

    // Incoming DAT and SNP have independent, unarbitrated return paths.
    chi_in.presponse_udata.pvalid = chi_in.psnoops.pvalid = 1;
    chi_in.presponse_udata.pbits.ptxn_uid = 5;
    chi_in.psnoops.pbits.ptxn_uid = 6;
    acquire_dat_in.pready = 1;
    snoops_in.pready = 0;
    eval();
    CHECK(chi_out.presponse_udata.pready && !chi_out.psnoops.pready);
    CHECK(acquire_dat_out.pvalid && acquire_dat_out.pbits.ptxn_uid == 5);
    CHECK(snoops_out.pvalid && snoops_out.pbits.ptxn_uid == 6);
    tick_model();
    chi_in.presponse_udata.pvalid = acquire_dat_in.pready = 0;
    snoops_in.pready = 1;
    eval();
    CHECK(!chi_out.presponse_udata.pready && chi_out.psnoops.pready);
    CHECK(!acquire_dat_out.pvalid && snoops_out.pvalid);
    tick_model();
    check_graph();
    graph.validate();
  });
}
