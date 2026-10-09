// SPDX-License-Identifier: Apache-2.0
#include "../../../../cores/rv5stage/tests/rsim/fetch-behavior.hpp"
extern "C" void event_frontend_sample(
    unsigned reset, unsigned clear, unsigned recovery, unsigned restart,
    std::uint64_t restart_pc, unsigned request_fire,
    std::uint64_t request_address, unsigned response_valid, unsigned replay,
    unsigned bits, unsigned page_fault, unsigned access_fault,
    unsigned fetched_valid, unsigned ready, std::uint64_t pc,
    std::uint64_t sequential_pc, std::uint64_t predicted_next_pc);
extern "C" void event_frontend_check();
extern "C" void event_frontend_bind();
extern "C" void event_frontend_finish();
int main() {
  return run_test([] {
    event_frontend_bind();
    fetch_before_tick = [] {
      event_frontend_sample(
          ((reset)&low_mask(32)), ((memory_out.pflush) & low_mask(32)),
          ((flush || restart_valid || invalidate_all) & low_mask(32)),
          ((restart_valid)&low_mask(32)), restart_pc,
          ((memory_out.prequest.pvalid && memory_in.prequest.pready) &
           low_mask(32)),
          memory_out.prequest.pbits.paddress,
          ((memory_in.presponse.pvalid) & low_mask(32)),
          ((memory_in.presponse.pbits.preplay) & low_mask(32)),
          memory_in.presponse.pbits.presponse.pword,
          ((memory_in.presponse.pbits.presponse.ppage_ufault) & low_mask(32)),
          ((memory_in.presponse.pbits.presponse.paccess_ufault) & low_mask(32)),
          ((fetched_out.pvalid) & low_mask(32)), ((fetched_ready)&low_mask(32)),
          fetched_out.pbits.ppc, fetched_out.pbits.psequential_upc,
          fetched_out.pbits.ppredicted_unext_upc);
    };
    tick_extra = event_frontend_check;
    run_fetch_test();
    rising();
    event_frontend_finish();
  });
}
