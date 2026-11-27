// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "test-sites.h"

int main() {
  return run_test([] {
    auto& graph = rheg::graph();
    graph.bind_manifest(rheg_generated::manifest());
    reset = 1;
    tick_model();
    reset = 0;

    auto request = [&](unsigned index) {
      auto& req = port_in.preq;
      req.pvalid = 1;
      req.pbits.popcode = 4; // ReadNoSnp
      req.pbits.ptgt_uid = 9;
      req.pbits.psrc_uid = 3;
      req.pbits.ptxn_uid = 10 + index;
      req.pbits.paddress = 64 * index;
      req.pbits.psize_uor_unum_ureq = 6; // Four 16-byte DAT beats
      req.pbits.preturn_unid_uor_ustash_unid_uor_udata_utarget = 3;
      req.pbits.preturn_utxn_uid_uor_ustash_ulpid = 20 + index;
      eval();
      for (unsigned wait = 0; !port_out.preq.pready && wait < 32; ++wait) tick_model();
      CHECK(port_out.preq.pready);
      tick_model();
      req = {};
      CHECK(graph.nodes.contains({site_request, index}));
    };
    auto& ready = port_in.pdat.presponse.pready;
    const auto& dat = port_out.pdat.presponse;
    ready = 0;
    request(0);
    request(1);
    CHECK(graph.nodes.size() == 2 && graph.edges.empty());

    for (unsigned i = 0; i < 8; ++i) {
      const unsigned owner = i / 4;
      eval();
      for (unsigned wait = 0; !dat.pvalid && wait < 32; ++wait) tick_model();
      CHECK(dat.pvalid);
      const auto held_data = dat.pbits.pdata;
      for (unsigned stall = 0; stall < 3; ++stall) {
        CHECK(dat.pvalid && dat.pbits.popcode == 4); // CompData
        CHECK(dat.pbits.pdata == held_data && dat.pbits.pbyte_uenable == 0xffff);
        CHECK(dat.pbits.ptxn_uid == 20 + owner && dat.pbits.pdata_uid == i % 4);
        tick_model();
        CHECK(graph.nodes.size() == 2 + i && graph.edges.size() == i);
      }
      ready = 1;
      eval();
      CHECK(dat.pvalid && dat.pbits.ptxn_uid == 20 + owner && dat.pbits.pdata_uid == i % 4);
      tick_model();
      ready = 0;
      rheg::Ref child{site_read_data, i};
      CHECK(graph.nodes.contains(child) && !graph.nodes.at(child).ancestry_unknown);
      CHECK(graph.edges.contains({{site_request, owner}, child}));
      CHECK(graph.nodes.size() == 3 + i && graph.edges.size() == i + 1);
    }
    ready = 1;
    tick_model();
    CHECK(!dat.pvalid && graph.nodes.size() == 10 && graph.edges.size() == 8);
    graph.validate();
  });
}
