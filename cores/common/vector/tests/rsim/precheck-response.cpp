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

    unsigned responses = 0, admissions = 0, checks = 0;
    std::set<std::pair<rheg::Ref, rheg::Ref>> expected_edges;
    auto cycle = [&](bool exact, bool valid, bool decision, bool normal_ready, bool exact_ready) {
      exact_owner = exact;
      response_in.pvalid = valid;
      response_in.pbits = decision;
      admission_in.pready = normal_ready;
      checker_in.pready = exact_ready;
      eval();
      const bool ready = exact ? exact_ready : normal_ready;
      CHECK(bool(response_out.pready) == ready);
      CHECK(bool(admission_out.pvalid) == (valid && !exact));
      CHECK(bool(checker_out.pvalid) == (valid && exact));
      if (valid) CHECK(bool(exact ? checker_out.pbits : admission_out.pbits) == decision);

      tick_model();
      if (valid && ready) {
        const rheg::Ref parent{site_response, responses++};
        const rheg::Ref child = exact ? rheg::Ref{site_checker, checks++}
                                      : rheg::Ref{site_admission, admissions++};
        CHECK(graph.nodes.at(parent).cycle == graph.nodes.at(child).cycle);
        CHECK(!graph.nodes.at(child).ancestry_unknown);
        expected_edges.emplace(parent, child);
      }
      CHECK(graph.edges == expected_edges);
      CHECK(graph.nodes.size() == 2 * responses);
    };

    // An idle route can be ready. A waiting offer can only be released by its owner.
    cycle(false, false, false, true, false);
    cycle(false, true, false, false, true);
    cycle(false, true, false, false, false);
    cycle(false, true, false, true, true);
    cycle(true, true, true, true, false);
    cycle(true, true, true, false, false);
    cycle(true, true, true, false, true);
    // Consecutive handoffs change owner and exercise both decision values on each route.
    cycle(false, true, true, true, false);
    cycle(true, true, false, true, true);
    cycle(true, false, false, false, true);
    CHECK(admissions == 2 && checks == 2);
    graph.validate();
  });
}
