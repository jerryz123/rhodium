// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "test-sites.h"

int main() {
  return run_test([] {
    rheg::graph().bind_manifest(rheg_generated::manifest());
    reset = 1;
    tick_model();
    reset = 0;
    a_in.pvalid = b_in.pvalid = 1;
    a_in.pbits = 0x31;
    b_in.pbits = 0x42;
    control_in.pvalid = persistent_in.pvalid = 1;
    const auto& graph = rheg::graph();
    unsigned events = 0, first = 0, second = 0;
    auto cycle = [&](unsigned grant, bool ready) {
      choose = grant;
      result_in.pready = allow = ready;
      eval();
      const bool from_a = grant == 1;
      const bool from_b = grant == 2;
      const bool valid = (from_a && a_in.pvalid) || (from_b && b_in.pvalid);
      const bool transfer = ready && valid;
      CHECK(bool(result_out.pvalid) == valid);
      CHECK(bool(a_out.pready) == (from_a && ready));
      CHECK(bool(b_out.pready) == (from_b && ready));
      CHECK(bool(b_transfer_out.pvalid) == (from_b && transfer));
      CHECK(bool(pulses_0_out.pvalid) == (ready && control_in.pvalid));
      CHECK(bool(pulses_1_out.pvalid) == (ready && persistent_in.pvalid));
      for (auto* tap : {&accepted_0_out, &accepted_1_out}) {
        CHECK(bool(tap->pvalid) == transfer);
        if (transfer) CHECK(tap->pbits == (from_a ? a_in.pbits : b_in.pbits));
      }
      tick_model();
      if (transfer) {
        rheg::Ref parent{from_a ? site_first : site_second, from_a ? first++ : second++};
        rheg::Ref forwarded{site_forwarded, events};
        CHECK(graph.edges.contains({parent, forwarded}));
        CHECK(graph.edges.contains({parent, {site_at_source, events}}));
        CHECK(graph.edges.contains({forwarded, {site_at_sink, events}}));
        ++events;
      }
      CHECK(graph.edges.size() == 3 * events);
      CHECK(graph.nodes.size() == 4 * events);
    };
    cycle(0, true);
    cycle(1, false);
    cycle(2, false);
    cycle(2, false);
    cycle(2, true);
    cycle(1, true);
    cycle(1, true);
    a_in.pvalid = control_in.pvalid = persistent_in.pvalid = 0;
    cycle(1, true);
    cycle(0, true);
    cycle(2, true);
  });
}
