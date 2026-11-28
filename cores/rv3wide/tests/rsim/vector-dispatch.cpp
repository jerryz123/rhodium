// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset=1; tick_model(); reset=0;
    address_in.pready=0; work_in.pready=0;
    window_pages[0]=0x23; window_pages[1]=0x71;
    auto offer=[&](unsigned owner,bool mem,bool store=false) {
      ingress_in.pvalid=1; ingress_in.pbits={}; direct=mem;
      auto& e=ingress_in.pbits;
      e.powner=owner;
      auto& d=e.pchecked.pdescriptor;
      d.ppc=0x1000+4*owner; d.pscalar=store ? 0xffc : 0x800;
      d.pstate.pvl=8; d.pcontrol.pmemory=mem; d.pcontrol.pstore=store;
      d.pcontrol.pdispatch_ukind=mem ? (store ? 3 : 2) : 1;
      eval();
    };
    auto probe=[&](unsigned page,unsigned offset,bool store=false) {
      scalar_probe_in.pvalid=1; auto& s=scalar_probe_in.pbits;
      s={}; s.pstore=store; s.ppages[0]=page; s.pfirst=s.plast=offset;
      eval(); return bool(scalar_conflict);
    };
    offer(0,true,true); CHECK(ingress_out.pready && !address_out.pvalid); tick_model();
    ingress_in.pvalid=0; window_pages[1]=0x90; eval();
    // Before the store enters VSAQ, a scalar load aliases either translated page,
    // never a disjoint byte or a later live translation of the second page.
    CHECK(address_out.pvalid && address_out.pbits.pissue.powner==0);
    CHECK(probe(0x23,0xfff) && probe(0x71,3) && !probe(0x71,4) && !probe(0x90,0));
    // A full VMDQ cannot partially enqueue a younger load on the register side.
    offer(1,true); CHECK(!ingress_out.pready); tick_model();
    address_in.pready=1; eval(); CHECK(ingress_out.pready && address_out.pbits.pissue.powner==0);
    tick_model(); ingress_in.pvalid=0; eval();
    CHECK(address_out.pvalid && address_out.pbits.pissue.powner==1);
    CHECK(!probe(0x23,0x800) && probe(0x23,0x800,true) && !probe(0x71,0));
    // VXDQ is still stalled on the older store: memory has gone ahead independently.
    CHECK(work_out.pvalid && work_out.pbits.pentry.powner==0);
    offer(2,false); CHECK(ingress_out.pready); tick_model();
    offer(3,false); CHECK(ingress_out.pready); tick_model();
    offer(0,true); CHECK(!ingress_out.pready && !address_out.pvalid);
    tick_model(); ingress_in.pvalid=0; work_in.pready=1;
    for (unsigned i=0;i<4;++i) {
      eval(); CHECK(!address_out.pvalid && work_out.pvalid && work_out.pbits.pentry.powner==i);
      CHECK(work_out.pbits.pdirect==(i<2)); tick_model();
    }
    eval(); CHECK(!work_out.pvalid && !address_out.pvalid);
  });
}
