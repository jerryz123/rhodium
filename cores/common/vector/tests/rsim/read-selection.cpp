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
    normal_in.pvalid = fold_in.pvalid = 1;
    normal_in.pbits.pleft = 7;
    fold_in.pbits.pleft = 11;
    unsigned reads = 0, normals = 0, folds = 0;
    auto cycle = [&](bool folding, bool ready) {
      fold_busy = folding;
      read_in.pready = ready;
      eval();
      CHECK(read_out.pvalid && read_out.pbits.pleft == (folding ? 11 : 7));
      CHECK(bool(normal_out.pready) == (ready && !folding));
      CHECK(bool(fold_out.pready) == (ready && folding));
      tick_model();
      if (ready) {
        rheg::Ref parent{folding ? site_fold : site_normal, folding ? folds++ : normals++};
        CHECK(graph.edges.contains({parent, {site_read, reads++}}));
      }
      CHECK(graph.edges.size() == reads && graph.nodes.size() == 2 * reads);
    };
    cycle(false, false);
    cycle(false, false);
    cycle(false, true);
    cycle(true, false);
    cycle(true, false);
    cycle(true, true);
    cycle(false, false);
    cycle(true, true);
    cycle(false, true);
    graph.validate();
  });
}
