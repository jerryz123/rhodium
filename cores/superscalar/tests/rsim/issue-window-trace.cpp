// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "test-sites.h"
#include <array>
#include <deque>

int main() {
  return run_test([] {
    rheg::graph().bind_manifest(rheg_generated::manifest());
    reset = 1;
    tick_model();
    reset = 0;
    for (auto& entry : ingress_in.pbits.pentries) entry.psequential_upc = 2;
    std::deque<unsigned> parents;
    unsigned packets = 0;
    std::array<unsigned, 2> issued{};
    const auto& graph = rheg::graph();
    auto cycle = [&](unsigned append, unsigned remove, bool flush = false) {
      ingress_in.pvalid = append != 0;
      ingress_in.pbits.pcount = append;
      consume = remove;
      clear_in.pvalid = flush;
      eval();
      const bool ready = !flush && parents.size() - remove <= 6;
      CHECK(bool(ingress_out.pready) == ready);
      tick_model();
      if (flush) parents.clear();
      for (unsigned lane = 0; lane < remove; ++lane) {
        CHECK(graph.edges.contains({{site_packet, parents.front()},
                                    {lane == 0 ? site_lane0 : site_lane1, issued[lane]++}}));
        parents.pop_front();
      }
      if (append && ready) {
        parents.insert(parents.end(), append, packets++);
      }
      CHECK(graph.edges.size() == issued[0] + issued[1]);
      CHECK(graph.nodes.size() == packets + graph.edges.size());
    };
    for (unsigned i = 0; i < 4; ++i) cycle(2, 0);
    cycle(2, 0);
    cycle(2, 0);
    cycle(2, 1); // One free slot still backpressures a two-lane input.
    cycle(1, 2); // Head lanes now come from different accepted packets.
    cycle(0, 2);
    cycle(2, 0, true);
    cycle(1, 0);
    cycle(2, 0);
    cycle(0, 2);
    cycle(0, 1);
  });
}
