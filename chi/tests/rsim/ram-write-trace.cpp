// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "test-sites.h"
#include <set>

int main() {
  return run_test([] {
    auto& graph = rheg::graph();
    graph.bind_manifest(rheg_generated::manifest());
    reset = 1;
    tick_model();
    reset = 0;
    std::set<std::pair<rheg::Ref, rheg::Ref>> expected;
    unsigned occurrences = 0, responses = 0, beats = 0;
    auto check_graph = [&] {
      CHECK(graph.nodes.size() == occurrences && graph.edges == expected);
    };
    auto request = [&](unsigned owner, unsigned address, unsigned size) {
      auto& req = port_in.preq;
      req.pvalid = 1;
      req.pbits.popcode = 29; // WriteNoSnpFull
      req.pbits.psrc_uid = 3;
      req.pbits.ptgt_uid = 9;
      req.pbits.ptxn_uid = 10 + owner;
      req.pbits.paddress = address;
      req.pbits.psize_uor_unum_ureq = size;
      eval();
      CHECK(port_out.preq.pready);
      tick_model();
      req = {};
      ++occurrences;
      CHECK(graph.nodes.contains({site_req, owner}));
      check_graph();
    };
    auto response = [&](unsigned owner, unsigned opcode, const std::vector<rheg::Ref>& parents) {
      auto& ready = port_in.prsp.pready;
      const auto& rsp = port_out.prsp;
      eval();
      for (unsigned wait = 0; !rsp.pvalid && wait < 32; ++wait) tick_model();
      CHECK(rsp.pvalid);
      const unsigned slot = rsp.pbits.pdbid_uor_ugroup_uid;
      for (unsigned stall = 0; stall < 3; ++stall) {
        CHECK(!ready && rsp.pvalid && rsp.pbits.popcode == opcode);
        CHECK(rsp.pbits.ptxn_uid == 10 + owner && rsp.pbits.pdbid_uor_ugroup_uid == slot);
        tick_model();
        check_graph();
      }
      ready = 1;
      eval();
      CHECK(rsp.pvalid && rsp.pbits.ptxn_uid == 10 + owner && rsp.pbits.popcode == opcode);
      tick_model();
      ready = 0;
      rheg::Ref child{site_rsp, responses++};
      CHECK(!graph.nodes.at(child).ancestry_unknown);
      for (const auto& parent : parents) expected.emplace(parent, child);
      ++occurrences;
      check_graph();
      return slot;
    };
    std::vector<rheg::Ref> receipts[3];
    auto send = [&](unsigned owner, unsigned slot, unsigned data_id, unsigned dbid_sequence) {
      auto& dat = port_in.pdat.prequest;
      dat.pvalid = 1;
      dat.pbits.popcode = 3; // NonCopyBackWriteData
      dat.pbits.psrc_uid = 3;
      dat.pbits.ptgt_uid = 9;
      dat.pbits.ptxn_uid = slot;
      dat.pbits.pdata_uid = data_id;
      dat.pbits.pbyte_uenable = 0xffff;
      eval();
      for (unsigned wait = 0; !port_out.pdat.prequest.pready && wait < 32; ++wait) tick_model();
      CHECK(port_out.pdat.prequest.pready);
      tick_model();
      dat = {};
      rheg::Ref beat{site_rxdat, beats};
      expected.emplace(rheg::Ref{site_sender, beats++}, beat);
      expected.emplace(rheg::Ref{site_rsp, dbid_sequence}, beat);
      receipts[owner].push_back(beat);
      CHECK(!graph.nodes.at(beat).ancestry_unknown);
      occurrences += 2;
      check_graph();
    };

    request(0, 0, 6);
    request(1, 64, 6);
    const auto a = response(0, 6, {{site_req, 0}}); // DBIDResp
    const auto b = response(1, 6, {{site_req, 1}});
    CHECK(a != b);
    // A DAT credit return with the same low TxnID bits has no DBID owner.
    port_in.pdat.prequest = {};
    port_in.pdat.prequest.pvalid = 1;
    port_in.pdat.prequest.pbits.popcode = 0; // DataLCrdReturn
    port_in.pdat.prequest.pbits.ptxn_uid = a;
    eval();
    CHECK(port_out.pdat.prequest.pready);
    tick_model();
    port_in.pdat.prequest = {};
    expected.emplace(rheg::Ref{site_sender, beats}, rheg::Ref{site_rxdat, beats});
    ++beats;
    occurrences += 2;
    check_graph();
    // Finish B before A, with unrelated and out-of-order DataIDs in each slot.
    send(0, a, 3, 0);
    send(1, b, 1, 1);
    send(0, a, 1, 0);
    send(1, b, 3, 1);
    send(1, b, 0, 1);
    send(0, a, 0, 0);
    send(1, b, 2, 1);
    // Hold B's offered Comp while A transfers its final DAT.
    eval();
    for (unsigned wait = 0; !port_out.prsp.pvalid && wait < 32; ++wait) tick_model();
    CHECK(!port_in.prsp.pready && port_out.prsp.pvalid);
    CHECK(port_out.prsp.pbits.popcode == 4 && port_out.prsp.pbits.pdbid_uor_ugroup_uid == b);
    check_graph();
    send(0, a, 2, 0);
    CHECK(response(1, 4, receipts[1]) == b); // Comp
    CHECK(response(0, 4, receipts[0]) == a);

    // Reuse the released DBID for a one-beat write.
    request(2, 0, 4);
    const auto reused = response(2, 6, {{site_req, 2}});
    CHECK(reused == a || reused == b);
    send(2, reused, 0, 4);
    CHECK(response(2, 4, receipts[2]) == reused);
    graph.validate();
  });
}
