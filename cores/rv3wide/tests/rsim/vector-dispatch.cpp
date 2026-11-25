// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"

int main() {
  return run_test([] {
    reset = 1;
    ingress_in = {};
    heads_0_in = {}; heads_1_in = {};
    tick_model();
    reset = 0;
    native_execute_in.pready=1;
    native_access=1;
    unsigned sequence = 0;
    auto offer = [&](unsigned path) {
      ingress_in.pvalid = 1;
      auto &e = ingress_in.pbits;
      e.powner = sequence % 4;
      e.pchecked.pdescriptor.ppc = 4 * sequence;
      enum DispatchKind { None, Execute, Load, Store };
      e.pchecked.pdescriptor.pcontrol.pdispatch_ukind = path == 0 ? Load : path == 1 ? Store : Execute;
      e.pchecked.powns_uwindow = path == 1;
      eval();
    };
    auto push = [&](unsigned path) {
      offer(path);
      CHECK(ingress_out.pready);
      tick_model();
      ++sequence;
      ingress_in.pvalid = 0;
    };
    for (unsigned n = 0; n < 4; ++n) {
      unsigned first = sequence;
      push(1); push(0); push(2);
      memory_busy[0]=memory_busy[1]=1;
      offer(1);
      heads_0_in.pready = 1;
      eval();
      CHECK(!ingress_out.pready); // Can't dispatch past a full matching queue.
      CHECK(heads_0_out.pvalid);
      CHECK(heads_0_out.pbits.pchecked.pdescriptor.ppc == 4 * (first+1));
      tick_model(); // Both younger heads issue, without consuming the older store.
      eval();
      CHECK(!heads_0_out.pvalid && !ingress_out.pready);
      CHECK(heads_1_out.pvalid && heads_1_out.pbits.powner == first%4);
      CHECK(heads_1_out.pbits.pchecked.powns_uwindow);
      heads_1_in.pready = 1;
      tick_model(); eval();
      CHECK(!ingress_out.pready && !heads_1_out.pvalid);
      offer(0); CHECK(!ingress_out.pready); // VLSeq still owns the write.
      native_execute_in.pready=0; offer(2); CHECK(!ingress_out.pready);
      native_execute_in.pready=1; memory_busy[1]=0;
      ingress_in.pvalid=0;
      memory_busy[0]=0;
      for (unsigned path=0;path<3;++path) { offer(path); CHECK(ingress_out.pready); }
      ingress_in.pvalid=0;
      heads_0_in.pready = heads_1_in.pready = 0;
    }
    // Native execute has no dispatch head. Its transfer is the VXSeq transfer.
    native_execute_in.pready=0; offer(2);
    CHECK(!ingress_out.pready && native_execute_out.pvalid);
    CHECK(native_execute_out.pbits.powner==sequence%4);
    native_execute_in.pready=1; eval(); CHECK(ingress_out.pready);
    push(2);

    // The precise checker did not install a VLSU cursor. Its kind stays reserved
    // until the matching macro is reclaimed, even with both native entries idle.
    native_access=0;
    const unsigned owner=sequence%4;
    push(0);
    heads_0_in.pready=1;
    tick_model();
    reclaimed_in.pvalid=1; reclaimed_in.pbits=(owner+1)%4;
    tick_model(); offer(0); CHECK(!ingress_out.pready);
    reclaimed_in.pbits=owner; tick_model(); offer(0); CHECK(ingress_out.pready);
  });
}
