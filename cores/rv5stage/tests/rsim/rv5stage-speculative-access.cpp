// Checks one-cycle MEM qualification at the RV5Stage guest-aware physical lookup port.
// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "../../../common/cache/tests/rsim/speculative-access.hpp"

int main() {
  return run_test([] {
    reset = 1;
    tick_model();
    reset = 0;
    state.pprivilege = 1;
    state.pguest.phstatus = 0x100;
    check_speculative_data_permissions([](unsigned addr, unsigned op, unsigned mode, unsigned expected) {
      auto &req = pipeline_in.prequest;
      req.pbits.pmemory.paddress = addr;
      req.pbits.pmemory.paccess = op;
      req.pbits.pmemory.pwidth = 3;
      req.pbits.pmemory.pbyte_umask = 0xff;
      req.pbits.ptranslation = mode;
      req.pvalid = 1;
      eval();
      CHECK(pipeline_lookup_out.pvalid && pipeline_lookup_out.pbits.paddress == addr &&
            !pipeline_out.presponse.pvalid);
      tick_model();
      req.pvalid = 0;
      eval();
      CHECK(pipeline_memory_out.prequest.pvalid == speculative_access::issues_lookup(expected));
      pipeline_memory_in.presponse.pvalid = pipeline_memory_out.prequest.pvalid;
      pipeline_memory_in.presponse.pbits.poutcome = op == speculative_access::store ? speculative_access::store_hit : speculative_access::load_hit;
      eval();
      CHECK(pipeline_out.presponse.pvalid && pipeline_out.presponse.pbits.poutcome == expected);
      CHECK(!core_memory_out.prequest.pvalid && !walker_memory_out.prequest.pvalid &&
            !pipeline_guest_fault.ppage_ufault);
      tick_model();
      pipeline_memory_in.presponse.pvalid = 0;
    });
  });
}
