// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "test-sites.h"

int main() {
  return run_test([] {
    rheg::graph().bind_manifest(rheg_generated::manifest());
    reset=1; tick_model(); reset=0;

    // A resolved stream starts with a normal mask beat. Its checked successor
    // can replace the cursor only when the last fragment is accepted.
    start_in.pvalid=1;
    start_in.pbits.pissue.pkind=6; // ResolvedStream
    start_in.pbits.pissue.pcommand.playout.peew=1;
    start_in.pbits.pissue.pcommand.paddress=4;
    normal_in.pvalid=1;
    normal_in.pbits.ppayload.penabled=3;
    normal_in.pbits.ppayload.plast=1;
    eval(); CHECK(start_out.pready && normal_out.pready);
    tick_model(); start_in.pvalid=0;
    // Queue another normal row while the first transfers. It competes with
    // the gathered offer and must stay queued until a new normal launch.
    element_in.pvalid=1; element_in.pbits.paddress=31;
    auto check_offer = [](unsigned address, unsigned mask, bool accept,
                          bool normal_fire, bool element_ready) {
      memory_in.prequest.pready=accept;
      eval();
      CHECK(memory_out.prequest.pvalid && !memory_out.prequest.pbits.pstore);
      CHECK(memory_out.prequest.pbits.paddress==address);
      CHECK(memory_out.prequest.pbits.pbyte_umask==mask);
      CHECK(bool(normal_accepted)==normal_fire && bool(element_out.pready)==element_ready);
      tick_model();
    };
    for (unsigned i=0;i<3;++i) check_offer(0,0x30,false,false,false);
    check_offer(0,0x30,true,true,true);
    normal_in.pvalid=0; element_in.pbits.paddress=40;
    for (unsigned i=0;i<3;++i) check_offer(16,0x8000,false,false,false);
    check_offer(16,0x8000,true,false,false);
    for (unsigned i=0;i<3;++i) check_offer(32,1,false,false,false);
    check_offer(32,1,true,false,true);
    element_in.pvalid=0;
    check_offer(32,0x300,true,false,true);

    start_in.pvalid=1; start_in.pbits.pissue.pcommand.paddress=48;
    eval(); CHECK(start_out.pready && !memory_out.prequest.pvalid);
    tick_model(); start_in.pvalid=0;
    check_offer(48,3,true,true,true);

    const auto& graph=rheg::graph();
    constexpr auto agen=row_sites::vector_load_agen;
    auto check_parent=[&](unsigned child, unsigned site, unsigned sequence) {
      CHECK(graph.nodes.contains({agen,child}));
      unsigned parents=0;
      for (const auto& [parent,target]:graph.edges) {
        if (target.site==agen && target.sequence==child) {
          CHECK(parent.site==site && parent.sequence==sequence);
          ++parents;
        }
      }
      CHECK(parents==1);
    };
    check_parent(0,row_sites::test_normal,0);
    check_parent(1,row_sites::test_element,0);
    check_parent(2,row_sites::test_element,0);
    check_parent(3,row_sites::test_element,1);
    check_parent(4,row_sites::test_normal,1);
    CHECK(!graph.nodes.contains({agen,5}));
  });
}
